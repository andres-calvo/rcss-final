"""
Servidor Flask + SocketIO para el panel de control web.

Proporciona endpoints WebSocket para:
- Iniciar/detener simulaciones
- Recibir estado del juego en tiempo real
- Logs del sistema
- Modo debug: enviar comandos y ver logs del jugador
"""

import os
import logging
from typing import Dict, Any, Optional, Callable
from flask import Flask, send_from_directory
from flask_socketio import SocketIO, emit

logger = logging.getLogger(__name__)

# Ruta al frontend
FRONTEND_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), '../../../frontend'))


class FlaskServer:
    """
    Servidor Flask con SocketIO para el panel de control.
    
    Eventos WebSocket:
    - simulation/start: Iniciar simulación
    - simulation/stop: Detener simulación
    - game/status: Estado del juego (servidor -> cliente)
    - system/log: Logs del sistema (servidor -> cliente)
    - debug/command: Comando manual desde UI (cliente -> servidor)
    - player/log: Logs del jugador (servidor -> cliente)
    """
    
    def __init__(self, host: str = "0.0.0.0", port: int = 5001):
        self.host = host
        self.port = port
        
        self.app = Flask(__name__)
        self.app.config['SECRET_KEY'] = 'robocup-secret-key'
        
        self.socketio = SocketIO(
            self.app,
            cors_allowed_origins="*",
            async_mode='threading'
        )
        
        # Callbacks para eventos
        self.on_start_simulation: Optional[Callable[[Dict[str, Any]], bool]] = None
        self.on_stop_simulation: Optional[Callable[[], None]] = None
        self.on_debug_command: Optional[Callable[[str, Dict[str, Any]], None]] = None
        
        self._setup_routes()
        self._setup_socket_events()
    
    def _setup_routes(self):
        """Configura rutas HTTP."""
        
        @self.app.route('/')
        def index():
            return send_from_directory(FRONTEND_DIR, 'index.html')
        
        @self.app.route('/<path:filename>')
        def static_files(filename):
            return send_from_directory(FRONTEND_DIR, filename)
        
        @self.app.route('/health')
        def health():
            return {'status': 'ok'}
        
        @self.app.route('/api/scenarios')
        def get_scenarios():
            return {
                'scenarios': [
                    {'id': 'dribbling', 'name': 'Dribbling', 'players': 1},
                    {'id': 'striker', 'name': 'Striker', 'players': 1},
                    {'id': 'passing', 'name': 'Pases', 'players': 2},
                    {'id': 'goalkeeper', 'name': 'Goalkeeper', 'players': 2},
                    {'id': 'defense', 'name': 'Defensa', 'players': 2}
                ]
            }
    
    def _setup_socket_events(self):
        """Configura eventos WebSocket."""
        
        @self.socketio.on('connect')
        def handle_connect():
            logger.info("Client connected")
            emit('system/log', {'msg': 'Connected to server', 'level': 'INFO'})
        
        @self.socketio.on('disconnect')
        def handle_disconnect():
            logger.info("Client disconnected")
        
        @self.socketio.on('simulation/start')
        def handle_start(data):
            """Maneja solicitud de inicio de simulación."""
            logger.info(f"Start simulation requested: {data}")
            
            if self.on_start_simulation:
                success = self.on_start_simulation(data)
                if success:
                    emit('game/status', {
                        'state': 'RUNNING',
                        'scenario': data.get('type'),
                        'time': 0
                    })
                    emit('system/log', {'msg': 'Simulation started', 'level': 'INFO'})
                else:
                    emit('system/log', {'msg': 'Failed to start simulation', 'level': 'ERROR'})
            else:
                emit('system/log', {'msg': 'No handler configured', 'level': 'WARNING'})
        
        @self.socketio.on('simulation/stop')
        def handle_stop(data=None):
            """Maneja solicitud de detención de simulación."""
            logger.info("Stop simulation requested")
            
            if self.on_stop_simulation:
                self.on_stop_simulation()
                emit('game/status', {'state': 'STOPPED'})
                emit('system/log', {'msg': 'Simulation stopped', 'level': 'INFO'})
        
        @self.socketio.on('debug/command')
        def handle_debug_command(data):
            """Maneja comando de debug desde la UI."""
            device_id = data.get('device_id', 'ESP_01')
            action = data.get('action', '')
            params = data.get('params', [])
            
            logger.info(f"Debug command: {action}({params}) -> {device_id}")
            
            if self.on_debug_command:
                self.on_debug_command(device_id, {'action': action, 'params': params})
                emit('system/log', {'msg': f'Command sent: {action}', 'level': 'INFO'})
            else:
                emit('system/log', {'msg': 'No debug handler configured', 'level': 'WARNING'})
    
    def emit_game_status(self, status: Dict[str, Any]) -> None:
        """
        Emite estado del juego a todos los clientes.
        
        Args:
            status: Estado con state, score, time, etc.
        """
        self.socketio.emit('game/status', status)
    
    def emit_log(self, message: str, level: str = "INFO") -> None:
        """
        Emite un log a todos los clientes.
        
        Args:
            message: Mensaje de log
            level: Nivel (INFO, WARNING, ERROR)
        """
        self.socketio.emit('system/log', {'msg': message, 'level': level})
    
    def emit_player_log(self, device_id: str, message: str, log_type: str = "info") -> None:
        """
        Emite un log del jugador a todos los clientes.
        
        Args:
            device_id: ID del dispositivo/jugador
            message: Mensaje del log
            log_type: Tipo de log (info, see, hear, cmd, error)
        """
        self.socketio.emit('player/log', {
            'device_id': device_id,
            'message': message,
            'type': log_type
        })
    
    def run(self, debug: bool = False) -> None:
        """Inicia el servidor."""
        logger.info(f"Starting Flask server on {self.host}:{self.port}")
        self.socketio.run(self.app, host=self.host, port=self.port, debug=debug)
