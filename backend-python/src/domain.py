"""
Domain - Lógica de simulación y escenarios.

Este módulo contiene la lógica de negocio para gestionar
simulaciones RoboCup y sus diferentes escenarios.
"""

import subprocess
import socket as socket_lib
import time
from enum import Enum
from dataclasses import dataclass, field
from typing import Optional, Dict, Any, List, Callable
import logging

logger = logging.getLogger(__name__)


class GameStatus(Enum):
    """Estados posibles del juego."""
    IDLE = "IDLE"
    BEFORE_KICK_OFF = "BEFORE_KICK_OFF"
    PLAYING = "PLAYING"
    FINISHED = "FINISHED"


class PlayerRole(Enum):
    """Roles disponibles para jugadores."""
    STRIKER = "STRIKER"
    DRIBBLER = "DRIBBLER"
    PASSER = "PASSER"
    RECEIVER = "RECEIVER"
    GOALKEEPER = "GOALKEEPER"
    DEFENDER = "DEFENDER"


class ScenarioType(Enum):
    """Tipos de escenarios de simulación."""
    DRIBBLING = "dribbling"
    STRIKER = "striker"
    PASSING = "passing"
    GOALKEEPER = "goalkeeper"
    DEFENSE = "defense"


@dataclass
class Player:
    """Representa un jugador en la simulación."""
    device_id: str
    role: PlayerRole
    team_name: str = "TeamA"
    uniform_number: int = 1
    is_connected: bool = False
    
    # Socket UDP para comunicación con RCSSServer
    sock: Optional[socket_lib.socket] = field(default=None, repr=False)
    server_address: tuple = field(default=("127.0.0.1", 6000), repr=False)


@dataclass
class SimulationConfig:
    """Configuración de una simulación."""
    scenario_type: ScenarioType
    players: List[Player]
    timeout_seconds: int = 120
    rcss_host: str = "127.0.0.1"
    rcss_port: int = 6000


class RCSSConnection:
    """
    Gestiona la conexión UDP con RCSSServer.
    
    Cada jugador tiene su propio socket UDP para comunicarse
    con el simulador.
    """
    
    def __init__(self, host: str = "127.0.0.1", port: int = 6000):
        self.host = host
        self.port = port
        self.socket: Optional[socket_lib.socket] = None
        self.assigned_port: Optional[int] = None
    
    def connect(self, team_name: str, uniform_number: int, is_goalie: bool = False) -> bool:
        """
        Conecta un jugador al simulador.
        
        Args:
            team_name: Nombre del equipo
            uniform_number: Número de camiseta
            is_goalie: Si es el portero
            
        Returns:
            True si la conexión fue exitosa
        """
        try:
            self.socket = socket_lib.socket(socket_lib.AF_INET, socket_lib.SOCK_DGRAM)
            self.socket.settimeout(5.0)
            
            # Mensaje de inicialización (formato RCSS)
            if is_goalie:
                init_msg = f"(init {team_name} (version 15) (goalie))"
            else:
                init_msg = f"(init {team_name} (version 15))"
            logger.info(f"Sending to {self.host}:{self.port}: {init_msg}")
            self.socket.sendto(init_msg.encode(), (self.host, self.port))
            
            # Esperar respuesta del servidor con puerto asignado
            logger.info("Waiting for response from rcssserver...")
            response, server_addr = self.socket.recvfrom(4096)
            response_str = response.decode()
            logger.info(f"Response from {server_addr}: {response_str}")
            self.assigned_port = server_addr[1]
            
            # Ubicar jugador en el campo (posición inicial según número)
            # Posiciones aproximadas para cada jugador (más cerca del centro para pruebas)
            positions = {
                1: (-10, -5),   # Jugador 1 - cerca del centro
                2: (-10, 5),    # Jugador 2 - cerca del centro
                3: (-15, -10),  # Jugador 3
                4: (-15, 10),   # Jugador 4
                5: (-20, 0),    # Medio
                6: (-20, 15),   # Medio
                7: (-5, -15),   # Delantero
                8: (-5, 0),     # Striker principal
                9: (-5, 15),    # Delantero
                10: (-25, -10), # Defensa
                11: (-25, 10),  # Defensa
            }
            x, y = positions.get(uniform_number, (-10, 0))
            move_cmd = f"(move {x} {y})"
            logger.info(f"Moving player to ({x}, {y})")
            self.socket.sendto(move_cmd.encode(), (self.host, self.assigned_port))
            
            logger.info(f"Player connected: {team_name} #{uniform_number} on port {self.assigned_port}")
            return True
            
        except socket_lib.timeout:
            logger.error("Connection timeout - rcssserver not responding")
            return False
        except Exception as e:
            logger.error(f"Connection failed: {e}")
            return False
    
    def send_command(self, command: str) -> None:
        """Envía un comando al simulador."""
        if self.socket and self.assigned_port:
            self.socket.sendto(command.encode(), (self.host, self.assigned_port))
    
    def receive(self, timeout: float = 0.1) -> Optional[str]:
        """Recibe un mensaje del simulador."""
        if not self.socket:
            return None
        
        try:
            self.socket.settimeout(timeout)
            data, _ = self.socket.recvfrom(8192)
            return data.decode()
        except socket_lib.timeout:
            return None
        except Exception as e:
            logger.error(f"Receive error: {e}")
            return None
    
    def disconnect(self) -> None:
        """Cierra la conexión."""
        if self.socket:
            self.socket.close()
            self.socket = None


class TrainerConnection:
    """
    Conexión de Trainer (offline coach) para controlar el juego.
    
    Permite enviar comandos como:
    - (start) para iniciar el juego
    - (change_mode kick_off_l) para kickoff
    - (recover) para recuperar stamina
    - (change_mode play_on) para continuar
    """
    
    def __init__(self, host: str = "127.0.0.1", port: int = 6001):
        self.host = host
        self.port = port
        self.socket: Optional[socket_lib.socket] = None
        self.assigned_port: Optional[int] = None
    
    def connect(self) -> bool:
        """Conecta como trainer al servidor."""
        try:
            self.socket = socket_lib.socket(socket_lib.AF_INET, socket_lib.SOCK_DGRAM)
            self.socket.settimeout(5.0)
            
            # Mensaje de inicialización de trainer
            init_msg = "(init (version 15))"
            logger.info(f"Trainer connecting to {self.host}:{self.port}")
            self.socket.sendto(init_msg.encode(), (self.host, self.port))
            
            # Esperar respuesta
            response, server_addr = self.socket.recvfrom(4096)
            response_str = response.decode()
            logger.info(f"Trainer response: {response_str}")
            self.assigned_port = server_addr[1]
            
            return True
            
        except socket_lib.timeout:
            logger.warning("Trainer connection timeout - continuing without trainer")
            return False
        except Exception as e:
            logger.error(f"Trainer connection failed: {e}")
            return False
    
    def kickoff(self) -> None:
        """Inicia el juego (kickoff para equipo izquierdo)."""
        self._send_command("(change_mode kick_off_l)")
    
    def play_on(self) -> None:
        """Continúa el juego."""
        self._send_command("(change_mode play_on)")
    
    def recover(self) -> None:
        """Recupera stamina de los jugadores."""
        self._send_command("(recover)")
    
    def move_ball(self, x: float, y: float) -> None:
        """Mueve la bola a una posición específica."""
        self._send_command(f"(move (ball) {x} {y})")
    
    def _send_command(self, command: str) -> None:
        """Envía un comando al servidor."""
        if self.socket and self.assigned_port:
            logger.info(f"Trainer command: {command}")
            self.socket.sendto(command.encode(), (self.host, self.assigned_port))
    
    def disconnect(self) -> None:
        """Cierra la conexión."""
        if self.socket:
            self.socket.close()
            self.socket = None
class SimulationManager:
    """
    Gestor principal de simulaciones.
    
    Responsabilidades:
    - Iniciar/detener RCSSServer
    - Gestionar conexiones de jugadores
    - Evaluar condiciones de terminación
    - Coordinar comunicación MQTT
    """
    
    def __init__(self, rcss_host: str = "127.0.0.1", rcss_port: int = 6000):
        self.rcss_host = rcss_host
        self.rcss_port = rcss_port
        self.status = GameStatus.IDLE
        self.current_scenario: Optional[ScenarioType] = None
        self.players: Dict[str, Player] = {}
        self.connections: Dict[str, RCSSConnection] = {}
        self.trainer: Optional[TrainerConnection] = None
        self.rcss_process: Optional[subprocess.Popen] = None
        self.start_time: Optional[float] = None
        self.timeout_seconds: int = 120
        
        # Callbacks para notificaciones
        self.on_status_change: Optional[Callable[[GameStatus], None]] = None
        self.on_goal: Optional[Callable[[], None]] = None
    
    def start_rcss_server(self) -> bool:
        """
        Inicia el proceso RCSSServer.
        
        Returns:
            True si el servidor inició correctamente
        """
        try:
            self.rcss_process = subprocess.Popen(
                ['rcssserver'],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE
            )
            time.sleep(2)  # Esperar que el servidor esté listo
            
            if self.rcss_process.poll() is None:
                logger.info("RCSSServer started successfully")
                return True
            else:
                logger.error("RCSSServer failed to start")
                return False
                
        except FileNotFoundError:
            logger.error("rcssserver not found. Please install RCSSServer.")
            return False
        except Exception as e:
            logger.error(f"Error starting RCSSServer: {e}")
            return False
    
    def stop_rcss_server(self) -> None:
        """Detiene el proceso RCSSServer."""
        if self.rcss_process:
            self.rcss_process.terminate()
            self.rcss_process.wait(timeout=5)
            self.rcss_process = None
            logger.info("RCSSServer stopped")
    
    def _restart_rcss_server(self) -> None:
        """Mata cualquier rcssserver/rcssmonitor existente e inicia nuevos."""
        import os
        import signal
        
        # Matar cualquier rcssserver y rcssmonitor existentes
        for process_name in ['rcssmonitor', 'rcssserver']:
            try:
                result = subprocess.run(
                    ['pgrep', '-f', process_name],
                    capture_output=True,
                    text=True
                )
                if result.returncode == 0:
                    pids = result.stdout.strip().split('\n')
                    for pid in pids:
                        if pid:
                            try:
                                os.kill(int(pid), signal.SIGTERM)
                                logger.info(f"Killed existing {process_name} (PID {pid})")
                            except ProcessLookupError:
                                pass
            except Exception as e:
                logger.warning(f"Error killing {process_name}: {e}")
        
        time.sleep(1)  # Esperar a que terminen
        
        # Iniciar nuevo rcssserver
        try:
            self.rcss_process = subprocess.Popen(
                ['rcssserver'],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL
            )
            time.sleep(2)  # Esperar que inicie
            logger.info("RCSSServer started")
        except Exception as e:
            logger.error(f"Failed to start rcssserver: {e}")
            return
        
        # Iniciar rcssmonitor
        try:
            subprocess.Popen(
                ['rcssmonitor'],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL
            )
            time.sleep(0.5)
            logger.info("RCSSMonitor started")
        except Exception as e:
            logger.warning(f"Failed to start rcssmonitor: {e}")
    
    def start_simulation(self, config: SimulationConfig) -> bool:
        """
        Inicia una simulación con la configuración dada.
        
        Args:
            config: Configuración del escenario
            
        Returns:
            True si la simulación inició correctamente
        """
        if self.status == GameStatus.PLAYING:
            logger.warning("Simulation already running")
            return False
        
        self.current_scenario = config.scenario_type
        self.timeout_seconds = config.timeout_seconds
        
        # Matar cualquier rcssserver existente e iniciar uno nuevo
        self._restart_rcss_server()
        
        # Conectar trainer primero (puerto 6001 para offline coach)
        self.trainer = TrainerConnection(config.rcss_host, 6001)
        trainer_connected = self.trainer.connect()
        
        # Conectar jugadores
        for player in config.players:
            conn = RCSSConnection(config.rcss_host, config.rcss_port)
            is_goalie = player.role == PlayerRole.GOALKEEPER
            
            if conn.connect(player.team_name, player.uniform_number, is_goalie):
                player.is_connected = True
                self.players[player.device_id] = player
                self.connections[player.device_id] = conn
            else:
                logger.error(f"Failed to connect player {player.device_id}")
                return False
        
        # Enviar kickoff para iniciar el juego
        if trainer_connected:
            time.sleep(0.5)  # Pequeña pausa para que todos los jugadores estén listos
            self.trainer.kickoff()
            logger.info("Kickoff sent via trainer")
        
        # Estado inicial: BEFORE_KICK_OFF (esperando que se patee el balón)
        # Cambiará a PLAYING cuando el referee envíe play_on
        self.status = GameStatus.BEFORE_KICK_OFF
        self.start_time = time.time()
        
        if self.on_status_change:
            self.on_status_change(self.status)
        
        logger.info(f"Simulation started: {config.scenario_type.value}")
        return True
    
    def stop_simulation(self) -> None:
        """Detiene la simulación actual."""
        for conn in self.connections.values():
            conn.disconnect()
        
        if self.trainer:
            self.trainer.disconnect()
            self.trainer = None
        
        self.connections.clear()
        self.players.clear()
        self.status = GameStatus.FINISHED
        self.current_scenario = None
        
        if self.on_status_change:
            self.on_status_change(self.status)
        
        logger.info("Simulation stopped")
    
    def check_timeout(self) -> bool:
        """Verifica si la simulación excedió el timeout."""
        if self.start_time and self.status == GameStatus.PLAYING:
            elapsed = time.time() - self.start_time
            return elapsed >= self.timeout_seconds
        return False
    
    def check_objective_completed(self, scenario: ScenarioType, game_state: Dict[str, Any]) -> bool:
        """
        Evalúa si se cumplió el objetivo del escenario.
        
        Args:
            scenario: Tipo de escenario
            game_state: Estado actual del juego
            
        Returns:
            True si el objetivo se cumplió
        """
        # Implementar lógica específica por escenario
        if scenario == ScenarioType.STRIKER:
            # Objetivo: marcar gol
            return game_state.get('goal_scored', False)
        
        elif scenario == ScenarioType.DRIBBLING:
            # Objetivo: llevar balón al otro lado
            ball_x = game_state.get('ball_x', 0)
            return ball_x > 40  # Lado derecho del campo
        
        elif scenario == ScenarioType.PASSING:
            # Objetivo: completar pase y marcar gol
            return game_state.get('pass_completed', False) and game_state.get('goal_scored', False)
        
        elif scenario == ScenarioType.GOALKEEPER:
            # Objetivo: atajar N disparos
            saves = game_state.get('saves', 0)
            return saves >= 3
        
        elif scenario == ScenarioType.DEFENSE:
            # Objetivo: interceptar balón sin cometer falta
            return game_state.get('ball_intercepted', False) and not game_state.get('foul', False)
        
        return False
    
    def get_player_status(self, device_id: str) -> Dict[str, Any]:
        """
        Obtiene el estado para enviar a un jugador específico.
        
        Args:
            device_id: ID del dispositivo/jugador
            
        Returns:
            Dict con status, role y datos de sensores
        """
        player = self.players.get(device_id)
        if not player:
            return {'status': GameStatus.IDLE.value}
        
        return {
            'status': self.status.value,
            'role': player.role.value
        }
