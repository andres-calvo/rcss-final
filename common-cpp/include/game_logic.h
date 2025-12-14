#ifndef ROBOCUP_GAME_LOGIC_H
#define ROBOCUP_GAME_LOGIC_H

/**
 * @file game_logic.h
 * @brief Lógica de decisión del agente - SIMPLIFICADA.
 * 
 * Regla principal: Si el balón es visible, dash direccional hacia él.
 * Sin memoria, sin interpolación, sin lógica compleja.
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
    static constexpr float KICKABLE_DISTANCE = 0.7f;
    static constexpr float CATCHABLE_DISTANCE = 2.0f;
    static constexpr float SHOOTING_DISTANCE = 25.0f;
    static constexpr float KICK_POWER_SHOT = 100.0f;
    static constexpr float KICK_POWER_PASS = 50.0f;
};

/**
 * @brief Motor de lógica del agente - SIMPLIFICADO.
 */
class GameLogic {
public:
    GameLogic() : current_state_(AgentState::IDLE), dribble_cycle_(0) {}
    
    void reset() { 
        current_state_ = AgentState::IDLE;
        dribble_cycle_ = 0;
    }
    
    AgentState get_state() const { return current_state_; }
    
    /**
     * @brief Decide la próxima acción.
     * REGLA SIMPLE: Si ves el balón -> dash hacia él. Si no -> turn 30.
     */
    Action decide_action(const SensorData& sensors) {
        // Incrementar contador de ciclos para dribbling
        dribble_cycle_++;
        
        // Kickoff: ir a la bola y patear
        if (sensors.status == GameStatus::BEFORE_KICK_OFF) {
            return handle_kickoff(sensors);
        }
        
        // Si no está jugando, no hacer nada
        if (sensors.status != GameStatus::PLAYING) {
            current_state_ = AgentState::IDLE;
            return Action::none();
        }
        
        // Delegar según rol
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

private:
    AgentState current_state_;
    int dribble_cycle_;  // Contador para alternar entre kick y dash
    
    static constexpr float DRIBBLE_DISTANCE = 8.0f;  // Zona de dribble grande
    static constexpr int DRIBBLE_KICK_INTERVAL = 1;   // Patear CADA ciclo
    
    static float abs(float val) { return val < 0 ? -val : val; }
    
    // ========== COMPORTAMIENTO CENTRAL ==========
    
    /**
     * @brief Buscar balón: simplemente girar 30 grados.
     */
    Action search_ball() {
        current_state_ = AgentState::SEARCHING_BALL;
        return Action::turn(30);
    }
    
    /**
     * @brief Ir hacia el balón con DASH DIRECCIONAL o DRIBBLE si está cerca.
     */
    Action approach_ball(const ObjectInfo& ball) {
        // Si está en zona de dribble (cercano pero no pateable)
        if (ball.distance <= DRIBBLE_DISTANCE && ball.distance > GameConfig::KICKABLE_DISTANCE) {
            current_state_ = AgentState::DRIBBLING;
            
            // Alternar entre kick y dash para evitar free_kick_fault
            if (dribble_cycle_ % DRIBBLE_KICK_INTERVAL == 0) {
                // Patear suave hacia adelante para mantener control
                return Action::kick(25, 0);  // Kick más fuerte hacia adelante
            } else {
                // Dash hacia la bola con más potencia
                return Action::dash(80, ball.angle);
            }
        }
        
        // Lejos: dash a máxima potencia
        current_state_ = AgentState::APPROACHING_BALL;
        
        // Más cerca de la zona de dribble, reducir potencia
        float power = (ball.distance > 10.0f) ? 100.0f : 80.0f;
        return Action::dash(power, ball.angle);
    }
    
    /**
     * @brief Disparo a gol - siempre hacia adelante (ángulo 0) con máxima potencia
     */
    Action shoot_to_goal(const ObjectInfo& goal) {
        current_state_ = AgentState::SHOOTING;
        // Disparar hacia el gol o hacia adelante si no lo vemos bien
        float shoot_angle = goal.visible ? goal.angle : 0;
        return Action::kick(100, shoot_angle);
    }
    
    /**
     * @brief Dribbling: patear moderado hacia adelante
     */
    Action dribble_forward() {
        current_state_ = AgentState::DRIBBLING;
        return Action::kick(30, 0);  // Kick más fuerte para avanzar
    }
    
    // ========== LÓGICA POR ROL ==========
    
    Action decide_striker(const SensorData& sensors) {
        const auto& ball = sensors.ball;
        const auto& goal = sensors.goal;
        
        // PRIORIDAD 1: Si no veo balón -> buscar
        if (!ball.visible) {
            return search_ball();
        }
        
        // PRIORIDAD 2: Si estamos en rango de pateo -> disparar o driblear
        if (ball.distance <= GameConfig::KICKABLE_DISTANCE) {
            // Si vemos el gol y está relativamente cerca, DISPARAR
            if (goal.visible && goal.distance < GameConfig::SHOOTING_DISTANCE) {
                return shoot_to_goal(goal);
            }
            
            // Si no vemos gol pero la bola está muy cerca y venimos driblando,
            // disparar hacia adelante (probablemente cerca del arco)
            if (dribble_cycle_ > 50) {  // Llevamos rato jugando
                // Cada ciertos ciclos, intentar un disparo hacia adelante
                if (dribble_cycle_ % 20 == 0) {
                    current_state_ = AgentState::SHOOTING;
                    return Action::kick(100, 0);  // Disparo fuerte hacia adelante
                }
            }
            
            return dribble_forward();
        }
        
        // PRIORIDAD 3: Acercarse al balón (incluye dribbling automático si está cerca)
        return approach_ball(ball);
    }
    
    Action decide_dribbler(const SensorData& sensors) {
        const auto& ball = sensors.ball;
        
        if (!ball.visible) {
            return search_ball();
        }
        
        if (ball.distance > GameConfig::KICKABLE_DISTANCE) {
            return approach_ball(ball);
        }
        
        return dribble_forward();
    }
    
    Action decide_passer(const SensorData& sensors) {
        const auto& ball = sensors.ball;
        
        if (!ball.visible) {
            return search_ball();
        }
        
        if (ball.distance > GameConfig::KICKABLE_DISTANCE) {
            return approach_ball(ball);
        }
        
        // Pasar a compañero si visible
        if (sensors.teammate_count > 0 && sensors.teammates[0].visible) {
            current_state_ = AgentState::PASSING;
            return Action::kick(GameConfig::KICK_POWER_PASS, sensors.teammates[0].angle);
        }
        
        return dribble_forward();
    }
    
    Action decide_receiver(const SensorData& sensors) {
        const auto& ball = sensors.ball;
        const auto& goal = sensors.goal;
        
        if (!ball.visible) {
            return Action::turn(30);
        }
        
        if (ball.distance > GameConfig::KICKABLE_DISTANCE) {
            return approach_ball(ball);
        }
        
        if (goal.visible) {
            return shoot_to_goal(goal);
        }
        
        return Action::turn(30);
    }
    
    Action decide_goalkeeper(const SensorData& sensors) {
        const auto& ball = sensors.ball;
        
        if (!ball.visible) {
            return Action::none();
        }
        
        if (ball.distance < GameConfig::CATCHABLE_DISTANCE) {
            current_state_ = AgentState::CATCHING;
            return Action::catch_ball(ball.angle);
        }
        
        // Moverse hacia el balón si está cerca
        if (ball.distance < 10.0f) {
            return Action::dash(30, ball.angle);
        }
        
        return Action::none();
    }
    
    Action decide_defender(const SensorData& sensors) {
        const auto& ball = sensors.ball;
        
        if (!ball.visible) {
            return search_ball();
        }
        
        if (ball.distance < GameConfig::KICKABLE_DISTANCE) {
            // Despejar
            return Action::kick(GameConfig::KICK_POWER_SHOT, 0);
        }
        
        // Acercarse al balón
        current_state_ = AgentState::DEFENDING;
        return Action::dash(80, ball.angle);
    }
    
    // ========== KICKOFF ==========
    
    Action handle_kickoff(const SensorData& sensors) {
        const auto& ball = sensors.ball;
        
        if (!ball.visible) {
            return Action::turn(30);
        }
        
        // Si está en rango de pateo, patear SUAVE para iniciar juego
        if (ball.distance <= GameConfig::KICKABLE_DISTANCE) {
            return Action::kick(30, 0);  // Kickoff suave para mantener control
        }
        
        // Dash progresivo: más agresivo pero frenando cerca
        float power;
        if (ball.distance > 6.0f) {
            power = 100.0f;  // Lejos: máxima velocidad
        } else if (ball.distance > 3.0f) {
            power = 80.0f;   // Medio: alta velocidad
        } else if (ball.distance > 1.5f) {
            power = 50.0f;   // Cerca: reducir
        } else {
            power = 30.0f;   // Llegando: frenar
        }
        
        return Action::dash(power, ball.angle);
    }
};

} // namespace robocup

#endif // ROBOCUP_GAME_LOGIC_H

