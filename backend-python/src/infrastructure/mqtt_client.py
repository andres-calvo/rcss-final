"""
Cliente MQTT para comunicación con agentes C++.

Gestiona la publicación de estados del juego y
suscripción a acciones de los jugadores.
"""

import json
import logging
from typing import Optional, Callable, Dict, Any

import paho.mqtt.client as mqtt

logger = logging.getLogger(__name__)


class MQTTClient:
    """
    Cliente MQTT para comunicación bidireccional con agentes.
    
    Tópicos:
    - game/state/{device_id}: Backend -> Agente (sensores)
    - player/action/{device_id}: Agente -> Backend (acciones)
    - team/comm: Comunicación entre agentes
    """
    
    def __init__(
        self,
        broker_host: str = "localhost",
        broker_port: int = 1883,
        client_id: str = "robocup_backend"
    ):
        self.broker_host = broker_host
        self.broker_port = broker_port
        self.client_id = client_id
        
        # Usar callback API v2 con protocolo MQTT v3.1.1
        self.client = mqtt.Client(
            client_id=client_id,
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            protocol=mqtt.MQTTv311
        )
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.client.on_disconnect = self._on_disconnect
        
        self.is_connected = False
        
        # Callbacks para eventos
        self.on_player_action: Optional[Callable[[str, Dict[str, Any]], None]] = None
        self.on_team_message: Optional[Callable[[Dict[str, Any]], None]] = None
    
    def _on_connect(self, client, userdata, flags, reason_code, properties):
        """Callback al conectarse al broker."""
        if reason_code == 0:
            self.is_connected = True
            logger.info(f"Connected to MQTT broker at {self.broker_host}:{self.broker_port}")
            
            # Suscribirse a acciones de jugadores
            client.subscribe("player/action/+")
            client.subscribe("team/comm")
        else:
            logger.error(f"MQTT connection failed: {reason_code}")
    
    def _on_disconnect(self, client, userdata, flags, reason_code, properties):
        """Callback al desconectarse."""
        self.is_connected = False
        if reason_code != 0:
            logger.warning(f"Disconnected from MQTT broker (rc={reason_code})")
    
    def _on_message(self, client, userdata, msg):
        """Callback al recibir un mensaje."""
        try:
            topic = msg.topic
            payload = json.loads(msg.payload.decode())
            
            if topic.startswith("player/action/"):
                device_id = topic.split("/")[-1]
                if self.on_player_action:
                    self.on_player_action(device_id, payload)
                logger.debug(f"Action from {device_id}: {payload}")
            
            elif topic == "team/comm":
                if self.on_team_message:
                    self.on_team_message(payload)
                logger.debug(f"Team message: {payload}")
                
        except json.JSONDecodeError as e:
            logger.error(f"Invalid JSON in message: {e}")
        except Exception as e:
            logger.error(f"Error processing message: {e}")
    
    def connect(self) -> bool:
        """
        Conecta al broker MQTT.
        
        Returns:
            True si la conexión fue exitosa
        """
        try:
            self.client.connect(self.broker_host, self.broker_port, keepalive=60)
            self.client.loop_start()
            return True
        except Exception as e:
            logger.error(f"Failed to connect to MQTT broker: {e}")
            return False
    
    def disconnect(self) -> None:
        """Desconecta del broker."""
        self.client.loop_stop()
        self.client.disconnect()
        self.is_connected = False
    
    def publish_game_state(self, device_id: str, state: Dict[str, Any]) -> None:
        """
        Publica el estado del juego para un jugador.
        
        Args:
            device_id: ID del dispositivo destino
            state: Estado con sensores en formato JSON
        """
        topic = f"game/state/{device_id}"
        payload = json.dumps(state)
        self.client.publish(topic, payload, qos=1)
        logger.debug(f"Published state to {device_id}")
    
    def publish_team_message(self, message: Dict[str, Any]) -> None:
        """
        Publica un mensaje para todo el equipo.
        
        Args:
            message: Mensaje con sender, msg, target_coords
        """
        topic = "team/comm"
        payload = json.dumps(message)
        self.client.publish(topic, payload, qos=1)
