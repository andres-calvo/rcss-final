"""
Entry Point - Aplicación principal del backend.

Integra todos los componentes: Flask, MQTT, RCSSServer.
"""

import os
import sys
import logging
import threading
import sys
import logging
import threading
import time
from typing import Dict, Any

from dotenv import load_dotenv

from src.rcss_adapter import RCSSAdapter, SensorData
from src.domain import (
    SimulationManager, SimulationConfig, Player, 
    PlayerRole, ScenarioType, GameStatus
)
from src.infrastructure.mqtt_client import MQTTClient
from src.infrastructure.flask_server import FlaskServer

# Configurar logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

# Cargar variables de entorno
load_dotenv()


class RoboCupBackend:
    """
    Orquestador principal del sistema RoboCup.
    
    Integra:
    - FlaskServer: Panel de control web
    - MQTTClient: Comunicación con agentes C++
    - SimulationManager: Gestión de simulaciones
    - RCSSAdapter: Parser de mensajes del simulador
    """
    
    def __init__(self):
        # Configuración desde variables de entorno
        self.mqtt_host = os.getenv('MQTT_HOST', 'localhost')
        self.mqtt_port = int(os.getenv('MQTT_PORT', '1883'))
        self.flask_host = os.getenv('FLASK_HOST', '0.0.0.0')
        self.flask_port = int(os.getenv('FLASK_PORT', '5001'))
        self.rcss_host = os.getenv('RCSS_HOST', '127.0.0.1')
        self.rcss_port = int(os.getenv('RCSS_PORT', '6000'))
        
        # Componentes
        self.adapter = RCSSAdapter()
        self.sim_manager = SimulationManager(self.rcss_host, self.rcss_port)
        self.mqtt = MQTTClient(self.mqtt_host, self.mqtt_port)
        self.flask = FlaskServer(self.flask_host, self.flask_port)
        
        # Estado
        self.running = False
        self.game_loop_thread = None
        
        # Rate limiting para sensores
        self.sensor_cycle_count = {}
        self.SENSOR_PUBLISH_INTERVAL = 3  # Publicar cada 3 mensajes
        
        # Configurar callbacks
        self._setup_callbacks()
    
    def _setup_callbacks(self):
        """Configura callbacks entre componentes."""
        
        # Flask -> SimulationManager
        self.flask.on_start_simulation = self._handle_start_simulation
        self.flask.on_stop_simulation = self._handle_stop_simulation
        
        # Flask -> Debug commands
        self.flask.on_debug_command = self._handle_debug_command
        
        # MQTT -> Comandos
        self.mqtt.on_player_action = self._handle_player_action
        
        # SimulationManager -> Flask
        self.sim_manager.on_status_change = self._handle_status_change
    
    def _handle_start_simulation(self, data: Dict[str, Any]) -> bool:
        """Maneja solicitud de inicio desde web."""
        try:
            scenario_type = ScenarioType(data.get('type', 'striker'))
            roles_config = data.get('roles', {})
            
            # Construir lista de jugadores
            players = []
            player_num = 1
            
            for device_id, role_str in roles_config.items():
                role = PlayerRole[role_str.upper()]
                # Determinar equipo según el escenario
                team_name = "TeamA"
                if scenario_type == ScenarioType.GOALKEEPER:
                    if role == PlayerRole.GOALKEEPER:
                        team_name = "TeamB"
                    elif role == PlayerRole.STRIKER:
                        team_name = "TeamA"
                elif scenario_type == ScenarioType.DEFENSE:
                    # Enforce roles based on order: 1st = Striker (A), 2nd = Defender (B)
                    if player_num == 1:
                        role = PlayerRole.STRIKER
                        team_name = "TeamA"
                    else:
                        role = PlayerRole.DEFENDER
                        team_name = "TeamB"
                
                players.append(Player(
                    device_id=device_id,
                    role=role,
                    team_name=team_name,
                    uniform_number=player_num
                ))
                player_num += 1
            
            # Si no hay config de roles, crear jugador por defecto
            if not players:
                players.append(Player(
                    device_id="ESP_01",
                    role=PlayerRole.STRIKER,
                    uniform_number=1
                ))
            
            config = SimulationConfig(
                scenario_type=scenario_type,
                players=players,
                rcss_host=self.rcss_host,
                rcss_port=self.rcss_port
            )
            
            return self.sim_manager.start_simulation(config)
            
        except Exception as e:
            logger.error(f"Error starting simulation: {e}")
            return False
    
    def _handle_stop_simulation(self):
        """Maneja solicitud de detención desde web."""
        self.sim_manager.stop_simulation()
    
    def _handle_player_action(self, device_id: str, action: Dict[str, Any]):
        """Maneja acción recibida de un agente C++."""
        # Convertir a comando RCSS
        command = self.adapter.to_rcss_command(action)
        
        # Enviar al simulador
        conn = self.sim_manager.connections.get(device_id)
        if conn:
            conn.send_command(command)
            logger.debug(f"Sent to RCSS [{device_id}]: {command}")
            # Emit to debug panel
            self.flask.emit_player_log(device_id, f"→ {command}", "cmd")
    
    def _handle_debug_command(self, device_id: str, action: Dict[str, Any]):
        """Maneja comando de debug desde la UI web."""
        # Convertir a comando RCSS
        command = self.adapter.to_rcss_command(action)
        
        if not command:
            self.flask.emit_player_log(device_id, "Empty command", "error")
            return
        
        # Enviar al simulador
        conn = self.sim_manager.connections.get(device_id)
        if conn:
            conn.send_command(command)
            logger.info(f"Debug command sent [{device_id}]: {command}")
            self.flask.emit_player_log(device_id, f"→ {command}", "cmd")
        else:
            logger.warning(f"No connection for device {device_id}")
            self.flask.emit_player_log(device_id, f"No connection for {device_id}", "error")
    
    def _handle_status_change(self, status: GameStatus):
        """Notifica cambio de estado al frontend."""
        self.flask.emit_game_status({
            'state': status.value,
            'scenario': self.sim_manager.current_scenario.value if self.sim_manager.current_scenario else None
        })
    
    def _game_loop(self):
        """Loop principal del juego."""
        logger.info("Game loop started")
        
        while self.running:
            # Procesar si estamos en BEFORE_KICK_OFF o PLAYING
            if self.sim_manager.status not in (GameStatus.BEFORE_KICK_OFF, GameStatus.PLAYING):
                time.sleep(0.1)
                continue
            
            # Verificar timeout
            if self.sim_manager.check_timeout():
                logger.info("Simulation timeout")
                self.sim_manager.stop_simulation()
                continue
            
            # Procesar cada jugador conectado
            for device_id, conn in self.sim_manager.connections.items():
                # Recibir mensaje del simulador
                message = conn.receive(timeout=0.05)
                
                if message:
                    # Debug: mostrar primeros 100 caracteres del mensaje
                    logger.debug(f"RX [{device_id}]: {message[:100]}...")
                    
                    # Verificar si es un mensaje del referee para actualizar estado
                    if message.startswith("(hear"):
                        referee_state = self.adapter.parse_referee(message)
                        if referee_state:
                            self._handle_referee_state(referee_state)
                            # Emit referee message to debug panel
                            self.flask.emit_player_log(device_id, f"Referee: {referee_state}", "hear")
                    
                    # Parsear datos de sensores
                    if message.startswith("(see"):
                        sensor_data = self.adapter.parse_see(message)
                        player = self.sim_manager.players.get(device_id)
                        
                        # Rate limiting: solo publicar cada N ciclos
                        if device_id not in self.sensor_cycle_count:
                            self.sensor_cycle_count[device_id] = 0
                        self.sensor_cycle_count[device_id] += 1
                        
                        should_publish = (self.sensor_cycle_count[device_id] % self.SENSOR_PUBLISH_INTERVAL == 0)
                        
                        # Debug: mostrar si se encontró la bola (solo cuando publicamos)
                        if sensor_data.ball and should_publish:
                            logger.info(f"Ball detected: dist={sensor_data.ball.distance:.1f}, angle={sensor_data.ball.angle:.1f}")
                            # Emit ball info to debug panel
                            self.flask.emit_player_log(
                                device_id, 
                                f"Ball: dist={sensor_data.ball.distance:.1f}, angle={sensor_data.ball.angle:.1f}",
                                "see"
                            )
                        
                        if player and should_publish:
                            # Construir y publicar estado
                            state = self.adapter.to_json_sensors(
                                sensor_data,
                                role=player.role.value,
                                status=self.sim_manager.status.value
                            )
                            self.mqtt.publish_game_state(device_id, state)
            
            time.sleep(0.05)  # ~20 Hz
        
        logger.info("Game loop stopped")
    
    def _handle_referee_state(self, referee_state: str):
        """Maneja cambios de estado del referee."""
        old_status = self.sim_manager.status
        state_changed = False
        
        # Transición kick_off -> play_on
        if referee_state == "play_on":
            if old_status == GameStatus.BEFORE_KICK_OFF:
                self.sim_manager.status = GameStatus.PLAYING
                logger.info("State transition: BEFORE_KICK_OFF -> PLAYING")
                state_changed = True
                if self.sim_manager.on_status_change:
                    self.sim_manager.on_status_change(self.sim_manager.status)
        
        # Nuevo kickoff (después de gol, por ejemplo)
        elif referee_state in ("kick_off_l", "kick_off_r"):
            if old_status == GameStatus.PLAYING:
                self.sim_manager.status = GameStatus.BEFORE_KICK_OFF
                logger.info(f"State transition: PLAYING -> BEFORE_KICK_OFF ({referee_state})")
                state_changed = True
                if self.sim_manager.on_status_change:
                    self.sim_manager.on_status_change(self.sim_manager.status)
        
        # Detección de gol
        elif referee_state in ("goal_l", "goal_r"):
            logger.info(f"Goal detected: {referee_state}")
            if self.sim_manager.on_goal:
                self.sim_manager.on_goal()
        
        # CRÍTICO: Si hubo cambio de estado, enviar INMEDIATAMENTE a todos los agentes
        # Esto evita el race condition del throttle de sensores
        if state_changed:
            self._broadcast_state_immediately()
    
    def _broadcast_state_immediately(self):
        """
        Envía el estado actual INMEDIATAMENTE a todos los agentes.
        
        Se usa cuando hay cambios críticos de estado (BEFORE_KICK_OFF <-> PLAYING)
        para evitar el race condition del throttle de sensores.
        """
        logger.info(f"Broadcasting state immediately: {self.sim_manager.status.value}")
        
        for device_id, player in self.sim_manager.players.items():
            # Crear un estado mínimo con solo status y role
            state = {
                'status': self.sim_manager.status.value,
                'role': player.role.value,
                'sensors': {}  # Vacío, solo importa el status
            }
            self.mqtt.publish_game_state(device_id, state)
            logger.debug(f"Sent immediate state to {device_id}: {self.sim_manager.status.value}")
    
    def run(self):
        """Inicia el backend completo."""
        logger.info("Starting RoboCup Backend...")
        
        # Conectar a MQTT
        if not self.mqtt.connect():
            logger.warning("MQTT not available, continuing without it")
        
        # Iniciar game loop en thread separado
        self.running = True
        self.game_loop_thread = threading.Thread(target=self._game_loop, daemon=True)
        self.game_loop_thread.start()
        
        # Iniciar Flask (bloqueante)
        try:
            self.flask.run(debug=False)
        except KeyboardInterrupt:
            logger.info("Shutting down...")
        finally:
            self.shutdown()
    
    def shutdown(self):
        """Apaga el sistema."""
        self.running = False
        self.sim_manager.stop_simulation()
        self.sim_manager.stop_rcss_server()
        self.mqtt.disconnect()
        logger.info("Backend shutdown complete")


def main():
    """Entry point principal."""
    backend = RoboCupBackend()
    backend.run()


if __name__ == '__main__':
    main()
