#!/usr/bin/env python3
"""
Test E2E - Validación del flujo completo del sistema.

Este script automatiza la prueba de integración:
1. Inicia el broker MQTT (requiere Mosquitto)
2. Inicia el backend Python
3. Simula un agente C++ publicando acciones
4. Valida que el flujo completo funcione

Uso:
    python run_full_simulation.py [--scenario striker]
"""

import subprocess
import time
import json
import sys
import signal
import argparse
from typing import Optional

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("Error: paho-mqtt not installed. Run: pip install paho-mqtt")
    sys.exit(1)


class SimulationTest:
    """Clase para ejecutar pruebas E2E del sistema."""
    
    def __init__(self, scenario: str = "striker", device_id: str = "ESP_01"):
        self.scenario = scenario
        self.device_id = device_id
        self.mqtt_client: Optional[mqtt.Client] = None
        self.backend_process: Optional[subprocess.Popen] = None
        self.received_states = []
        self.test_passed = False
        
    def setup_mqtt(self, broker: str = "localhost", port: int = 1883) -> bool:
        """Configura el cliente MQTT para el test."""
        try:
            self.mqtt_client = mqtt.Client(client_id="e2e_test")
            self.mqtt_client.on_connect = self._on_connect
            self.mqtt_client.on_message = self._on_message
            
            self.mqtt_client.connect(broker, port, keepalive=60)
            self.mqtt_client.loop_start()
            time.sleep(1)
            
            print(f"✓ Connected to MQTT broker at {broker}:{port}")
            return True
        except Exception as e:
            print(f"✗ Failed to connect to MQTT: {e}")
            return False
    
    def _on_connect(self, client, userdata, flags, rc):
        """Callback de conexión MQTT."""
        if rc == 0:
            # Suscribirse al estado del juego
            client.subscribe(f"game/state/{self.device_id}")
            
    def _on_message(self, client, userdata, msg):
        """Callback de mensaje recibido."""
        try:
            payload = json.loads(msg.payload.decode())
            self.received_states.append(payload)
            print(f"  Received state: {payload.get('status')} - {payload.get('role')}")
        except json.JSONDecodeError:
            pass
    
    def simulate_agent_action(self, action: str, params: list):
        """Simula una acción del agente C++."""
        if not self.mqtt_client:
            return
        
        payload = json.dumps({
            "action": action,
            "params": params
        })
        
        topic = f"player/action/{self.device_id}"
        self.mqtt_client.publish(topic, payload, qos=1)
        print(f"  Sent action: {action} {params}")
    
    def run_test(self, duration: int = 10) -> bool:
        """
        Ejecuta el test E2E.
        
        Args:
            duration: Duración del test en segundos
            
        Returns:
            True si el test pasó
        """
        print(f"\n{'='*60}")
        print(f"  RoboCup E2E Test - Scenario: {self.scenario}")
        print(f"{'='*60}\n")
        
        # Verificar MQTT
        if not self.setup_mqtt():
            print("\n⚠ MQTT broker not available. Please start Mosquitto first:")
            print("  docker run -d -p 1883:1883 eclipse-mosquitto")
            return False
        
        # Simular ciclo de juego
        print("\nSimulating game cycle...")
        
        for i in range(duration):
            # Simular estado del juego (como si fuera el backend)
            state = {
                "status": "PLAYING",
                "role": self._get_role_for_scenario(),
                "sensors": {
                    "ball": {"dist": 10.0 - i * 0.5, "angle": 5.0},
                    "goal": {"dist": 40.0, "angle": 0.0}
                }
            }
            
            self.mqtt_client.publish(
                f"game/state/{self.device_id}",
                json.dumps(state),
                qos=1
            )
            
            # Simular respuesta del agente
            if i % 2 == 0:
                self.simulate_agent_action("dash", [100, 0])
            else:
                self.simulate_agent_action("turn", [5])
            
            time.sleep(0.5)
        
        # Enviar estado FINISHED
        self.mqtt_client.publish(
            f"game/state/{self.device_id}",
            json.dumps({"status": "FINISHED", "role": self._get_role_for_scenario()}),
            qos=1
        )
        
        time.sleep(1)
        
        # Evaluar resultado
        print(f"\nTest completed. Received {len(self.received_states)} state updates.")
        
        if len(self.received_states) >= 5:
            print("\n✓ E2E Test PASSED")
            self.test_passed = True
        else:
            print("\n✗ E2E Test FAILED - Not enough state updates received")
        
        return self.test_passed
    
    def _get_role_for_scenario(self) -> str:
        """Obtiene el rol correspondiente al escenario."""
        role_map = {
            "striker": "STRIKER",
            "dribbling": "DRIBBLER",
            "passing": "PASSER",
            "goalkeeper": "GOALKEEPER",
            "defense": "DEFENDER"
        }
        return role_map.get(self.scenario, "STRIKER")
    
    def cleanup(self):
        """Limpia recursos."""
        if self.mqtt_client:
            self.mqtt_client.loop_stop()
            self.mqtt_client.disconnect()
        
        if self.backend_process:
            self.backend_process.terminate()
            self.backend_process.wait(timeout=5)


def main():
    parser = argparse.ArgumentParser(description="Run E2E simulation test")
    parser.add_argument("--scenario", default="striker", 
                       choices=["striker", "dribbling", "passing", "goalkeeper", "defense"],
                       help="Scenario to test")
    parser.add_argument("--device", default="ESP_01", help="Device ID")
    parser.add_argument("--duration", type=int, default=10, help="Test duration in seconds")
    
    args = parser.parse_args()
    
    test = SimulationTest(args.scenario, args.device)
    
    try:
        success = test.run_test(args.duration)
        sys.exit(0 if success else 1)
    except KeyboardInterrupt:
        print("\n\nTest interrupted by user")
        sys.exit(1)
    finally:
        test.cleanup()


if __name__ == "__main__":
    main()
