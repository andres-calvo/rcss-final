"""
Tests para la lógica de dominio.
"""

import pytest
from src.domain import (
    SimulationManager, SimulationConfig, Player,
    PlayerRole, ScenarioType, GameStatus
)


class TestSimulationManager:
    """Tests para el gestor de simulaciones."""

    def setup_method(self):
        """Setup para cada test."""
        self.manager = SimulationManager()

    def test_initial_state_is_idle(self):
        """El estado inicial debe ser IDLE."""
        assert self.manager.status == GameStatus.IDLE

    def test_check_objective_striker_goal(self):
        """Escenario STRIKER: objetivo cumplido al marcar gol."""
        game_state = {'goal_scored': True}
        
        result = self.manager.check_objective_completed(
            ScenarioType.STRIKER, game_state
        )
        
        assert result is True

    def test_check_objective_striker_no_goal(self):
        """Escenario STRIKER: objetivo no cumplido sin gol."""
        game_state = {'goal_scored': False}
        
        result = self.manager.check_objective_completed(
            ScenarioType.STRIKER, game_state
        )
        
        assert result is False

    def test_check_objective_dribbling(self):
        """Escenario DRIBBLING: objetivo cumplido al cruzar campo."""
        game_state = {'ball_x': 45}
        
        result = self.manager.check_objective_completed(
            ScenarioType.DRIBBLING, game_state
        )
        
        assert result is True

    def test_check_objective_goalkeeper(self):
        """Escenario GOALKEEPER: objetivo cumplido con 3 atajadas."""
        game_state = {'saves': 3}
        
        result = self.manager.check_objective_completed(
            ScenarioType.GOALKEEPER, game_state
        )
        
        assert result is True

    def test_check_objective_defense_intercept_no_foul(self):
        """Escenario DEFENSE: interceptar sin falta."""
        game_state = {'ball_intercepted': True, 'foul': False}
        
        result = self.manager.check_objective_completed(
            ScenarioType.DEFENSE, game_state
        )
        
        assert result is True

    def test_check_objective_defense_foul(self):
        """Escenario DEFENSE: fallo si comete falta."""
        game_state = {'ball_intercepted': True, 'foul': True}
        
        result = self.manager.check_objective_completed(
            ScenarioType.DEFENSE, game_state
        )
        
        assert result is False

    def test_get_player_status_unknown_device(self):
        """Debe retornar IDLE para dispositivo desconocido."""
        result = self.manager.get_player_status("unknown_device")
        
        assert result['status'] == 'IDLE'


class TestPlayerRoles:
    """Tests para roles de jugadores."""

    def test_all_roles_defined(self):
        """Verificar que todos los roles están definidos."""
        expected_roles = ['STRIKER', 'DRIBBLER', 'PASSER', 'RECEIVER', 'GOALKEEPER', 'DEFENDER']
        
        for role_name in expected_roles:
            role = PlayerRole[role_name]
            assert role.value == role_name


class TestScenarioTypes:
    """Tests para tipos de escenarios."""

    def test_all_scenarios_defined(self):
        """Verificar que todos los escenarios están definidos."""
        scenarios = {
            'DRIBBLING': 'dribbling',
            'STRIKER': 'striker',
            'PASSING': 'passing',
            'GOALKEEPER': 'goalkeeper',
            'DEFENSE': 'defense'
        }
        
        for name, value in scenarios.items():
            scenario = ScenarioType[name]
            assert scenario.value == value
