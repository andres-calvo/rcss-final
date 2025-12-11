"""
Adaptador para RCSSServer - Parser de S-Expressions.

Este módulo traduce los mensajes del simulador RoboCup a estructuras
de datos Python y viceversa.
"""

import re
from dataclasses import dataclass
from typing import Optional, List, Dict, Any


@dataclass
class BallInfo:
    """Información de la bola relativa al jugador."""
    distance: float
    angle: float


@dataclass
class GoalInfo:
    """Información del gol relativa al jugador."""
    distance: float
    angle: float


@dataclass
class PlayerInfo:
    """Información de un jugador visible."""
    player_id: int
    distance: float
    angle: float


@dataclass
class SensorData:
    """Datos consolidados de sensores."""
    ball: Optional[BallInfo] = None
    goal: Optional[GoalInfo] = None
    teammates: Optional[List[PlayerInfo]] = None
    
    def __post_init__(self):
        if self.teammates is None:
            self.teammates = []


class RCSSAdapter:
    """
    Adaptador bidireccional para RCSSServer.
    
    Responsabilidades:
    - Parsear mensajes 'see', 'hear', 'sense_body' del simulador
    - Convertir datos a formato JSON para el agente C++
    - Generar comandos S-Expression para el simulador
    """

    # Patrones regex para parsing de S-Expressions
    BALL_PATTERN = re.compile(r'\(\(b\)\s+([\d.-]+)\s+([\d.-]+)\)')
    GOAL_PATTERN = re.compile(r'\(\(g\s+[rl]\)\s+([\d.-]+)\s+([\d.-]+)\)')
    PLAYER_PATTERN = re.compile(r'\(\(p\s+"[^"]+"\s+(\d+)\)\s+([\d.-]+)\s+([\d.-]+)\)')
    HEAR_PATTERN = re.compile(r'\(hear\s+\d+\s+(\d+)\s+"([^"]+)"\)')
    STAMINA_PATTERN = re.compile(r'\(stamina\s+(\d+)')
    SPEED_PATTERN = re.compile(r'\(speed\s+([\d.-]+)')

    def parse_see(self, message: str) -> SensorData:
        """
        Parsea un mensaje 'see' del simulador.
        
        Args:
            message: Mensaje S-Expression del tipo (see time ...)
            
        Returns:
            SensorData con información de bola, gol y jugadores visibles.
        """
        ball = None
        goal = None
        teammates = []

        # Buscar bola
        ball_match = self.BALL_PATTERN.search(message)
        if ball_match:
            ball = BallInfo(
                distance=float(ball_match.group(1)),
                angle=float(ball_match.group(2))
            )

        # Buscar gol
        goal_match = self.GOAL_PATTERN.search(message)
        if goal_match:
            goal = GoalInfo(
                distance=float(goal_match.group(1)),
                angle=float(goal_match.group(2))
            )

        # Buscar jugadores
        for player_match in self.PLAYER_PATTERN.finditer(message):
            teammates.append(PlayerInfo(
                player_id=int(player_match.group(1)),
                distance=float(player_match.group(2)),
                angle=float(player_match.group(3))
            ))

        return SensorData(ball=ball, goal=goal, teammates=teammates)

    def parse_hear(self, message: str) -> Dict[str, Any]:
        """
        Parsea un mensaje 'hear' (comunicación entre jugadores).
        
        Args:
            message: Mensaje S-Expression del tipo (hear time sender "msg")
            
        Returns:
            Dict con sender y message.
        """
        match = self.HEAR_PATTERN.search(message)
        if match:
            return {
                'sender': int(match.group(1)),
                'message': match.group(2)
            }
        return {'sender': None, 'message': None}

    def parse_sense_body(self, message: str) -> Dict[str, Any]:
        """
        Parsea un mensaje 'sense_body' (estado del jugador).
        
        Args:
            message: Mensaje S-Expression del tipo (sense_body time ...)
            
        Returns:
            Dict con stamina, speed, etc.
        """
        result = {}
        
        stamina_match = self.STAMINA_PATTERN.search(message)
        if stamina_match:
            result['stamina'] = int(stamina_match.group(1))
        
        speed_match = self.SPEED_PATTERN.search(message)
        if speed_match:
            result['speed'] = float(speed_match.group(1))
        
        return result

    def to_json_sensors(self, sensor_data: SensorData, role: str, status: str) -> Dict[str, Any]:
        """
        Convierte datos de sensores a formato JSON para el agente.
        
        Este es el formato que el agente C++ espera recibir via MQTT.
        
        Args:
            sensor_data: Datos parseados del simulador
            role: Rol asignado (STRIKER, DRIBBLER, etc.)
            status: Estado del juego (PLAYING, FINISHED, IDLE)
            
        Returns:
            Dict en formato JSON para el agente.
        """
        sensors = {}
        
        if sensor_data.ball:
            sensors['ball'] = {
                'dist': sensor_data.ball.distance,
                'angle': sensor_data.ball.angle
            }
        
        if sensor_data.goal:
            sensors['goal'] = {
                'dist': sensor_data.goal.distance,
                'angle': sensor_data.goal.angle
            }
        
        if sensor_data.teammates:
            sensors['teammates'] = [
                {'id': p.player_id, 'dist': p.distance, 'angle': p.angle}
                for p in sensor_data.teammates
            ]
        
        return {
            'status': status,
            'role': role,
            'sensors': sensors
        }

    def to_rcss_command(self, action: Dict[str, Any]) -> str:
        """
        Convierte una acción del agente a comando S-Expression.
        
        Formatos RCSS:
        - (dash <power>) o (dash <power> <direction>)
        - (turn <moment>)
        - (kick <power> <direction>)
        - (catch <direction>)
        - (move <x> <y>)
        
        Args:
            action: Dict con 'action' y 'params' del agente
            
        Returns:
            String con comando S-Expression para RCSSServer.
        """
        action_type = action.get('action', '').lower()
        params = action.get('params', [])
        
        if action_type == 'turn':
            # turn solo acepta un parámetro (momento/ángulo)
            angle = params[0] if params else 0
            return f"(turn {angle})"
        
        elif action_type == 'dash':
            # dash acepta power, y opcionalmente direction
            power = params[0] if len(params) > 0 else 100
            if len(params) > 1 and params[1] != 0:
                direction = params[1]
                return f"(dash {power} {direction})"
            return f"(dash {power})"
        
        elif action_type == 'kick':
            # kick requiere power y direction
            power = params[0] if len(params) > 0 else 100
            direction = params[1] if len(params) > 1 else 0
            return f"(kick {power} {direction})"
        
        elif action_type == 'catch':
            # catch requiere direction
            direction = params[0] if params else 0
            return f"(catch {direction})"
        
        elif action_type == 'move':
            # move requiere x e y (solo antes del kickoff)
            x = params[0] if len(params) > 0 else 0
            y = params[1] if len(params) > 1 else 0
            return f"(move {x} {y})"
        
        elif action_type == 'none' or not action_type:
            return ""
        
        # Fallback para acciones desconocidas
        if not params:
            return f"({action_type})"
        params_str = ' '.join(str(p) for p in params)
        return f"({action_type} {params_str})"
