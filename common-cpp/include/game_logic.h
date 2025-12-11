#ifndef ROBOCUP_GAME_LOGIC_H
#define ROBOCUP_GAME_LOGIC_H

/**
 * @file game_logic.h
 * @brief Lógica de decisión del agente - Núcleo compartido PC/ESP32.
 * 
 * Este archivo contiene la implementación de la IA del agente.
 * Es completamente agnóstico a la plataforma (sin dependencias de OS).
 */

#include "messages.h"

namespace robocup {

/**
 * @brief Estados de la máquina de estados finitos del agente.
 */
enum class AgentState : uint8_t {
    IDLE = 0,
    SEARCHING_BALL,
    APPROACHING_BALL,
    DRIBBLING,
    SHOOTING,
    PASSING,
    DEFENDING,
    CATCHING
};

/**
 * @brief Constantes de juego configurables.
 */
struct GameConfig {
    // Distancias
    static constexpr float KICKABLE_DISTANCE = 0.7f;    // Distancia para patear
    static constexpr float CATCHABLE_DISTANCE = 2.0f;   // Distancia para atrapar (goalie)
    static constexpr float SHOOTING_DISTANCE = 25.0f;   // Distancia para tirar a gol
    
    // Ángulos
    static constexpr float ANGLE_TOLERANCE = 10.0f;     // Tolerancia de alineación
    
    // Potencias
    static constexpr float DASH_POWER_HIGH = 100.0f;
    static constexpr float DASH_POWER_MEDIUM = 60.0f;
    static constexpr float DASH_POWER_LOW = 30.0f;
    static constexpr float KICK_POWER_SHOT = 100.0f;
    static constexpr float KICK_POWER_PASS = 50.0f;
};

/**
 * @brief Motor de lógica del agente.
 * 
 * Implementa la toma de decisiones basada en:
 * - Rol asignado (STRIKER, GOALKEEPER, etc.)
 * - Estado actual del mundo (SensorData)
 * - Estado interno del agente (FSM)
 */
class GameLogic {
public:
    GameLogic();
    
    /**
     * @brief Decide la próxima acción basándose en el estado actual.
     * 
     * Esta es la función principal que se llama en cada ciclo del juego.
     * 
     * @param sensors Datos de sensores actuales
     * @return Action a ejecutar
     */
    Action decide_action(const SensorData& sensors);
    
    /**
     * @brief Reinicia el estado del agente.
     */
    void reset();
    
    /**
     * @brief Obtiene el estado actual de la FSM.
     */
    AgentState get_state() const { return current_state_; }

private:
    AgentState current_state_;
    
    // Memoria de última posición conocida de la bola
    ObjectInfo last_known_ball_;
    int cycles_since_ball_seen_;
    static constexpr int MAX_MEMORY_CYCLES = 30;  // Recordar por 30 ciclos
    
    // Lógica específica por rol
    Action decide_striker(const SensorData& sensors);
    Action decide_dribbler(const SensorData& sensors);
    Action decide_passer(const SensorData& sensors);
    Action decide_receiver(const SensorData& sensors);
    Action decide_goalkeeper(const SensorData& sensors);
    Action decide_defender(const SensorData& sensors);
    
    // Comportamientos básicos
    Action search_ball();
    Action approach_ball(const ObjectInfo& ball);
    Action align_to_target(float target_angle);
    Action shoot_to_goal(const ObjectInfo& goal);
    Action dribble_forward(const ObjectInfo& ball);
    Action handle_kickoff(const SensorData& sensors);
    
    // Obtiene la bola actual o la última conocida
    ObjectInfo get_effective_ball(const SensorData& sensors);
    
    // Utilidades matemáticas
    static float normalize_angle(float angle);
    static float abs(float val);
};

// ============================================================================
// Implementación inline para permitir uso como header-only library
// ============================================================================

inline GameLogic::GameLogic() 
    : current_state_(AgentState::IDLE)
    , last_known_ball_()
    , cycles_since_ball_seen_(999) 
{}

inline void GameLogic::reset() {
    current_state_ = AgentState::IDLE;
    last_known_ball_ = ObjectInfo();
    cycles_since_ball_seen_ = 999;
}

inline float GameLogic::normalize_angle(float angle) {
    while (angle > 180.0f) angle -= 360.0f;
    while (angle < -180.0f) angle += 360.0f;
    return angle;
}

inline float GameLogic::abs(float val) {
    return val < 0 ? -val : val;
}

inline Action GameLogic::decide_action(const SensorData& sensors) {
    // Si el juego está en kickoff, ir a la bola y patear para iniciar
    if (sensors.status == GameStatus::BEFORE_KICK_OFF) {
        return handle_kickoff(sensors);
    }
    
    // Si el juego no está activo (IDLE o FINISHED), no hacer nada
    if (sensors.status != GameStatus::PLAYING) {
        current_state_ = AgentState::IDLE;
        return Action::none();
    }
    
    // Delegar según el rol
    switch (sensors.role) {
        case PlayerRole::STRIKER:
            return decide_striker(sensors);
        case PlayerRole::DRIBBLER:
            return decide_dribbler(sensors);
        case PlayerRole::PASSER:
            return decide_passer(sensors);
        case PlayerRole::RECEIVER:
            return decide_receiver(sensors);
        case PlayerRole::GOALKEEPER:
            return decide_goalkeeper(sensors);
        case PlayerRole::DEFENDER:
            return decide_defender(sensors);
        default:
            return Action::none();
    }
}

// ============================================================================
// Función auxiliar para obtener bola actual o recordada
// ============================================================================

inline ObjectInfo GameLogic::get_effective_ball(const SensorData& sensors) {
    if (sensors.ball.visible) {
        // Actualizar memoria con posición actual
        last_known_ball_ = sensors.ball;
        cycles_since_ball_seen_ = 0;
        return sensors.ball;
    }
    
    // Si no vemos la bola pero tenemos memoria reciente
    cycles_since_ball_seen_++;
    if (cycles_since_ball_seen_ < MAX_MEMORY_CYCLES && last_known_ball_.visible) {
        // Devolver última posición conocida (el ángulo puede cambiar ligeramente)
        return last_known_ball_;
    }
    
    // Sin memoria válida
    return ObjectInfo();  // visible = false
}

// ============================================================================
// Lógica del STRIKER: Buscar balón, acercarse, disparar a gol
// ============================================================================

inline Action GameLogic::decide_striker(const SensorData& sensors) {
    // Usar bola actual o recordada
    ObjectInfo ball = get_effective_ball(sensors);
    const auto& goal = sensors.goal;
    
    // Si no vemos la bola y no tenemos memoria, buscarla
    if (!ball.visible) {
        current_state_ = AgentState::SEARCHING_BALL;
        return search_ball();
    }
    
    // Si la bola está lejos, acercarse
    if (ball.distance > GameConfig::KICKABLE_DISTANCE) {
        current_state_ = AgentState::APPROACHING_BALL;
        return approach_ball(ball);
    }
    
    // Tenemos la bola, decidir qué hacer
    if (goal.visible && goal.distance < GameConfig::SHOOTING_DISTANCE) {
        current_state_ = AgentState::SHOOTING;
        return shoot_to_goal(goal);
    }
    
    // Driblear hacia adelante
    current_state_ = AgentState::DRIBBLING;
    return dribble_forward(ball);
}

// ============================================================================
// Lógica del DRIBBLER: Conducir balón de un lado a otro
// ============================================================================

inline Action GameLogic::decide_dribbler(const SensorData& sensors) {
    const auto& ball = sensors.ball;
    
    if (!ball.visible) {
        current_state_ = AgentState::SEARCHING_BALL;
        return search_ball();
    }
    
    if (ball.distance > GameConfig::KICKABLE_DISTANCE) {
        current_state_ = AgentState::APPROACHING_BALL;
        return approach_ball(ball);
    }
    
    // Driblear hacia el gol (lado derecho)
    current_state_ = AgentState::DRIBBLING;
    return dribble_forward(ball);
}

// ============================================================================
// Lógica del PASSER: Buscar balón y pasar a compañero
// ============================================================================

inline Action GameLogic::decide_passer(const SensorData& sensors) {
    const auto& ball = sensors.ball;
    
    if (!ball.visible) {
        current_state_ = AgentState::SEARCHING_BALL;
        return search_ball();
    }
    
    if (ball.distance > GameConfig::KICKABLE_DISTANCE) {
        current_state_ = AgentState::APPROACHING_BALL;
        return approach_ball(ball);
    }
    
    // Buscar compañero para pasar
    if (sensors.teammate_count > 0) {
        const auto& teammate = sensors.teammates[0];
        if (teammate.visible) {
            current_state_ = AgentState::PASSING;
            return Action::kick(GameConfig::KICK_POWER_PASS, teammate.angle);
        }
    }
    
    // Si no hay compañero, driblear
    return dribble_forward(ball);
}

// ============================================================================
// Lógica del RECEIVER: Esperar pase y rematar
// ============================================================================

inline Action GameLogic::decide_receiver(const SensorData& sensors) {
    const auto& ball = sensors.ball;
    const auto& goal = sensors.goal;
    
    if (!ball.visible) {
        // Esperar mirando hacia donde debería venir el pase
        return Action::turn(30);
    }
    
    // Si la bola viene hacia nosotros, prepararse
    if (ball.distance > GameConfig::KICKABLE_DISTANCE) {
        current_state_ = AgentState::APPROACHING_BALL;
        return approach_ball(ball);
    }
    
    // Tenemos la bola, disparar a gol
    if (goal.visible) {
        current_state_ = AgentState::SHOOTING;
        return shoot_to_goal(goal);
    }
    
    return Action::turn(30);
}

// ============================================================================
// Lógica del GOALKEEPER: Defender arco, atrapar y despejar
// ============================================================================

inline Action GameLogic::decide_goalkeeper(const SensorData& sensors) {
    const auto& ball = sensors.ball;
    
    if (!ball.visible) {
        // Quedarse en posición
        return Action::none();
    }
    
    // Si la bola está cerca, intentar atrapar
    if (ball.distance < GameConfig::CATCHABLE_DISTANCE) {
        current_state_ = AgentState::CATCHING;
        return Action::catch_ball(ball.angle);
    }
    
    // Moverse para interceptar
    if (abs(ball.angle) > GameConfig::ANGLE_TOLERANCE) {
        return Action::turn(ball.angle * 0.5f);
    }
    
    // Avanzar hacia la bola si está muy cerca
    if (ball.distance < 10.0f) {
        return Action::dash(GameConfig::DASH_POWER_LOW, ball.angle);
    }
    
    return Action::none();
}

// ============================================================================
// Lógica del DEFENDER: Interceptar sin cometer falta
// ============================================================================

inline Action GameLogic::decide_defender(const SensorData& sensors) {
    const auto& ball = sensors.ball;
    
    if (!ball.visible) {
        current_state_ = AgentState::SEARCHING_BALL;
        return search_ball();
    }
    
    // Acercarse a la bola para interceptar
    if (ball.distance > GameConfig::KICKABLE_DISTANCE * 2) {
        current_state_ = AgentState::DEFENDING;
        return approach_ball(ball);
    }
    
    // Si estamos cerca, intentar quitar la bola
    if (ball.distance < GameConfig::KICKABLE_DISTANCE) {
        // Despejar hacia el lado contrario
        return Action::kick(GameConfig::KICK_POWER_SHOT, 0);
    }
    
    // Seguir acercándose con cuidado
    return Action::dash(GameConfig::DASH_POWER_LOW, ball.angle);
}

// ============================================================================
// Comportamientos básicos compartidos
// ============================================================================

inline Action GameLogic::search_ball() {
    // Girar lentamente para buscar la bola (giro pequeño = más probabilidad de encontrarla)
    return Action::turn(15);
}

inline Action GameLogic::handle_kickoff(const SensorData& sensors) {
    // En kickoff, la bola está en el centro (0,0)
    // Debemos ir hacia ella y patearla para iniciar el juego
    
    ObjectInfo ball = get_effective_ball(sensors);
    
    // Si no vemos la bola, buscarla
    if (!ball.visible) {
        return Action::turn(15);
    }
    
    // Si estamos lejos de la bola, acercarnos
    if (ball.distance > GameConfig::KICKABLE_DISTANCE) {
        // Si el ángulo es muy grande, girar primero
        if (abs(ball.angle) > 30.0f) {
            return Action::turn(ball.angle * 0.5f);
        }
        // Dash hacia la bola
        return Action::dash(GameConfig::DASH_POWER_MEDIUM, ball.angle);
    }
    
    // Estamos cerca de la bola, patearla para iniciar el juego
    // Patear hacia adelante (dirección 0)
    return Action::kick(20, 0);
}

inline Action GameLogic::approach_ball(const ObjectInfo& ball) {
    // Si el ángulo es muy grande, primero girar
    if (abs(ball.angle) > 45.0f) {
        return Action::turn(ball.angle * 0.8f);
    }
    
    // Si el ángulo es moderado, hacer dash con dirección
    // (esto permite moverse mientras nos alineamos)
    float power = ball.distance > 5.0f 
        ? GameConfig::DASH_POWER_HIGH 
        : GameConfig::DASH_POWER_MEDIUM;
    
    // RCSS dash puede aceptar dirección, así que hacemos dash hacia la bola
    return Action::dash(power, ball.angle);
}

inline Action GameLogic::align_to_target(float target_angle) {
    if (abs(target_angle) > GameConfig::ANGLE_TOLERANCE) {
        return Action::turn(target_angle);
    }
    return Action::none();
}

inline Action GameLogic::shoot_to_goal(const ObjectInfo& goal) {
    // Si el gol no está alineado, girar
    if (abs(goal.angle) > GameConfig::ANGLE_TOLERANCE * 2) {
        return Action::turn(goal.angle);
    }
    
    // Disparar con toda la potencia
    return Action::kick(GameConfig::KICK_POWER_SHOT, goal.angle);
}

inline Action GameLogic::dribble_forward(const ObjectInfo& ball) {
    // Pequeños toques hacia adelante
    return Action::kick(20, 0);
}

} // namespace robocup

#endif // ROBOCUP_GAME_LOGIC_H
