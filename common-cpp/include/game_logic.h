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
#include "localization.h"

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
 * @brief Fases de la jugada coordinada de kickoff.
 */
enum class KickoffPhase : uint8_t {
    INITIAL = 0,         // Passer approaching ball, receiver running to position
    PASSER_HAS_BALL,     // Passer has ball, about to pass
    PASS_TO_RECEIVER,    // Ball passed, receiver should receive
    RECEIVER_HAS_BALL,   // Receiver has ball, dribbling
    RETURN_PASS,         // Receiver returning pass to passer
    PASSER_SHOOTS,       // Passer has ball for final shot
    COMPLETED            // Play finished
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
    GameLogic() : current_state_(AgentState::IDLE), dribble_cycle_(0), goal_search_cycles_(0), kickoff_phase_(KickoffPhase::INITIAL), receiver_run_cycles_(0) {}
    
    void reset() { 
        current_state_ = AgentState::IDLE;
        dribble_cycle_ = 0;
        goal_search_cycles_ = 0;
        kickoff_phase_ = KickoffPhase::INITIAL;
        receiver_run_cycles_ = 0;
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
    int goal_search_cycles_;  // Contador de ciclos buscando el arco
    KickoffPhase kickoff_phase_;
    int receiver_run_cycles_;
    
    static constexpr float DRIBBLE_DISTANCE = 5.0f;  // Zona de dribble grande
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
     * @brief Dribbling: patear hacia el arco enemigo usando triangulación.
     * Si tenemos posición válida, calcular dirección hacia el arco.
     * Si no, patear hacia adelante como fallback.
     */
    Action dribble_forward(const SensorData& sensors) {
        current_state_ = AgentState::DRIBBLING;
        
        // Si tenemos posición válida por triangulación, driblear hacia el arco
        if (sensors.position.valid) {
            // TODO: Aqui no estamos validando de que dentro de las banderas este el arco
            float angle_to_goal = Localization::angle_to_enemy_goal(sensors.position);
            return Action::kick(30, angle_to_goal);
        }
        
        return Action::kick(30, 0);  // Fallback: hacia adelante
    }
    
    // ========== LÓGICA POR ROL ==========
    
    Action decide_striker(const SensorData& sensors) {
        const auto& ball = sensors.ball;
        const auto& goal = sensors.goal;
        
        // PRIORIDAD 1: Si no veo balón -> buscar
        if (!ball.visible) {
            goal_search_cycles_ = 0;  // Reset búsqueda de arco
            return search_ball();
        }
        
        // PRIORIDAD 2: Si estamos en rango de pateo -> disparar o driblear
        if (ball.distance <= GameConfig::KICKABLE_DISTANCE) {
            // Si vemos el gol y está relativamente cerca, DISPARAR
            if (goal.visible && goal.distance < GameConfig::SHOOTING_DISTANCE) {
                goal_search_cycles_ = 0;
                return shoot_to_goal(goal);
            }
            
            // Si vemos el gol pero está lejos, driblear HACIA el arco
            if (goal.visible) {
                goal_search_cycles_ = 0;
                current_state_ = AgentState::DRIBBLING;
                return Action::kick(30, goal.angle);  // Dribble hacia el arco
            }
            
            // NO vemos el arco: usar triangulación mejorada si está disponible
            if (sensors.position.valid) {
                // TODO: Aqui no estamos validando de que dentro de las banderas este el arco
                // Si estamos en zona de gol (x > 35), disparar al centro del arco
                if (sensors.position.x > 35.0f) {
                    current_state_ = AgentState::SHOOTING;
                    // Calcular ángulo hacia el CENTRO del arco (52.5, 0)
                    float angle_to_goal = Localization::angle_to_target(
                        sensors.position, 52.5f, 0.0f);
                    return Action::kick(100, angle_to_goal);  // Disparo fuerte!
                }
                
                // Si estamos lejos, driblear hacia el arco usando triangulación
                float angle_to_goal = Localization::angle_to_enemy_goal(sensors.position);
                current_state_ = AgentState::DRIBBLING;
                return Action::kick(30, angle_to_goal);
            }
            
            // TODO: QUe pasa si por buscar el arco pierdo el balon?
            // Sin triangulación: girar para buscar el arco
            goal_search_cycles_++;
            if (goal_search_cycles_ < 5) {
                current_state_ = AgentState::SEARCHING_BALL;
                return Action::turn(30);  // Girar para buscar arco
            }
            
            // Fallback: después de 5 ciclos sin encontrar, dribble hacia adelante
            return dribble_forward(sensors);
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
        
        return dribble_forward(sensors);
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
        
        return dribble_forward(sensors);
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
        
        // Restringir movimiento: no salir del área (x > 35 o x < -35)
        if (sensors.position.valid) {
            if (abs(sensors.position.x) < 35.0f) {
                current_state_ = AgentState::DEFENDING;
                // Volver al arco (50 o -50 según lado actual)
                float target_x = (sensors.position.x > 0) ? 50.0f : -50.0f;
                float angle_to_home = Localization::angle_to_target(sensors.position, target_x, 0.0f);
                return Action::dash(80, angle_to_home);
            }
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

