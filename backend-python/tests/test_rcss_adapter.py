"""
Tests para el parser de S-Expressions de RCSSServer.

Siguiendo TDD: Primero los tests (RED), luego la implementación (GREEN).
"""

import pytest
from src.rcss_adapter import RCSSAdapter, SensorData, BallInfo, GoalInfo, PlayerInfo


class TestSExpressionParser:
    """Tests para parsing de S-Expressions del simulador."""

    def setup_method(self):
        """Setup para cada test."""
        self.adapter = RCSSAdapter()

    def test_parse_see_message_with_ball(self):
        """Debe parsear mensaje 'see' y extraer posición de la bola."""
        # Mensaje típico de visión del simulador
        see_msg = "(see 100 ((b) 10.5 -15.0) ((g r) 50.0 0.0))"
        
        result = self.adapter.parse_see(see_msg)
        
        assert result.ball is not None
        assert result.ball.distance == pytest.approx(10.5, rel=0.1)
        assert result.ball.angle == pytest.approx(-15.0, rel=0.1)

    def test_parse_see_message_with_goal(self):
        """Debe parsear mensaje 'see' y extraer posición del gol."""
        see_msg = "(see 100 ((b) 10.5 -15.0) ((g r) 50.0 0.0))"
        
        result = self.adapter.parse_see(see_msg)
        
        assert result.goal is not None
        assert result.goal.distance == pytest.approx(50.0, rel=0.1)
        assert result.goal.angle == pytest.approx(0.0, rel=0.1)

    def test_parse_see_message_without_ball(self):
        """Debe manejar mensaje sin bola visible."""
        see_msg = "(see 100 ((g r) 50.0 0.0))"
        
        result = self.adapter.parse_see(see_msg)
        
        assert result.ball is None
        assert result.goal is not None

    def test_parse_see_message_with_players(self):
        """Debe parsear jugadores visibles."""
        see_msg = "(see 100 ((b) 10.5 -15.0) ((p \"TeamA\" 2) 5.0 20.0) ((p \"TeamA\" 3) 8.0 -10.0))"
        
        result = self.adapter.parse_see(see_msg)
        
        assert len(result.teammates) == 2
        assert result.teammates[0].player_id == 2
        assert result.teammates[0].distance == pytest.approx(5.0, rel=0.1)

    def test_parse_hear_message(self):
        """Debe parsear mensajes de comunicación entre jugadores."""
        hear_msg = '(hear 100 2 "PASSING")'
        
        result = self.adapter.parse_hear(hear_msg)
        
        assert result['sender'] == 2
        assert result['message'] == "PASSING"

    def test_parse_sense_body(self):
        """Debe parsear información del cuerpo del jugador."""
        sense_msg = "(sense_body 100 (view_mode high normal) (stamina 8000 1) (speed 0.5 30.0))"
        
        result = self.adapter.parse_sense_body(sense_msg)
        
        assert result['stamina'] == 8000
        assert result['speed'] == pytest.approx(0.5, rel=0.1)

    def test_convert_to_json_sensors(self):
        """Debe convertir datos parseados a formato JSON para el agente."""
        sensor_data = SensorData(
            ball=BallInfo(distance=10.5, angle=-15.0),
            goal=GoalInfo(distance=50.0, angle=0.0),
            teammates=[PlayerInfo(player_id=2, distance=5.0, angle=20.0)]
        )
        
        json_output = self.adapter.to_json_sensors(sensor_data, role="STRIKER", status="PLAYING")
        
        assert json_output['status'] == "PLAYING"
        assert json_output['role'] == "STRIKER"
        assert json_output['sensors']['ball']['dist'] == pytest.approx(10.5, rel=0.1)
        assert json_output['sensors']['ball']['angle'] == pytest.approx(-15.0, rel=0.1)


class TestRCSSCommands:
    """Tests para generación de comandos hacia RCSSServer."""

    def setup_method(self):
        """Setup para cada test."""
        self.adapter = RCSSAdapter()

    def test_generate_dash_command(self):
        """Debe generar comando dash correcto."""
        action = {"action": "dash", "params": [100, 30]}
        
        command = self.adapter.to_rcss_command(action)
        
        assert command == "(dash 100 30)"

    def test_generate_turn_command(self):
        """Debe generar comando turn correcto."""
        action = {"action": "turn", "params": [45]}
        
        command = self.adapter.to_rcss_command(action)
        
        assert command == "(turn 45)"

    def test_generate_kick_command(self):
        """Debe generar comando kick correcto."""
        action = {"action": "kick", "params": [100, 0]}
        
        command = self.adapter.to_rcss_command(action)
        
        assert command == "(kick 100 0)"

    def test_generate_catch_command(self):
        """Debe generar comando catch (para goalkeeper)."""
        action = {"action": "catch", "params": [-30]}
        
        command = self.adapter.to_rcss_command(action)
        
        assert command == "(catch -30)"

    def test_generate_move_command(self):
        """Debe generar comando move (posicionamiento inicial)."""
        action = {"action": "move", "params": [-10, 0]}
        
        command = self.adapter.to_rcss_command(action)
        
        assert command == "(move -10 0)"
