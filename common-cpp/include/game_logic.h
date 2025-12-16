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
    static constexpr float CATCHABLE_DISTANCE_GK_SIM = 3.0f;  // Distancia mayor para simulación de arquero
    static constexpr float SHOOTING_DISTANCE = 25.0f;
    static constexpr float KICK_POWER_SHOT = 100.0f;
    static constexpr float KICK_POWER_PASS = 50.0f;
};

/**
 * @brief Motor de lógica del agente - SIMPLIFICADO.
 */
class GameLogic {
public:
    GameLogic() : current_state_(AgentState::IDLE), dribble_cycle_(0), goal_search_cycles_(0), kickoff_phase_(KickoffPhase::INITIAL), receiver_run_cycles_(0), passer_kicked_(false), goalkeeper_caught_(false), goalkeeper_turned_(false) {}
    
    void reset() { 
        current_state_ = AgentState::IDLE;
        dribble_cycle_ = 0;
        goal_search_cycles_ = 0;
        kickoff_phase_ = KickoffPhase::INITIAL;
        receiver_run_cycles_ = 0;
        passer_kicked_ = false;
        goalkeeper_caught_ = false;
        goalkeeper_turned_ = false;
    }
    
    AgentState get_state() const { return current_state_; }
    
    /**
     * @brief Decide la próxima acción.
     * REGLA SIMPLE: Si ves el balón -> dash hacia él. Si no -> turn 30.
     */
    Action decide_action(const SensorData& sensors) {
        // Incrementar contador de ciclos para dribbling
        dribble_cycle_++;
        
        // Kickoff: SOLO el PASSER puede moverse, el resto debe esperar
        if (sensors.status == GameStatus::BEFORE_KICK_OFF) {
            // SOLO el PASSER hace el kickoff, el resto espera quieto
            if (sensors.role == PlayerRole::PASSER) {
                return handle_passer_kickoff(sensors);
            }
            // RECEIVER y todos los demás deben esperar quietos hasta play_on
            current_state_ = AgentState::IDLE;
            return Action::none();
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
            case PlayerRole::STRIKER_GK_SIM:
                return decide_striker_gk_sim(sensors);
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
    bool passer_kicked_;  // Flag para saber si el PASSER ya hizo kickoff
    bool goalkeeper_caught_;  // Flag para evitar múltiples catches (penalty)
    bool goalkeeper_turned_;  // Flag para girar hacia el centro una sola vez
    
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
        // PASSER solo hace kickoff UNA VEZ, luego no hace absolutamente nada
        
        // Si ya hizo kickoff, SIEMPRE retornar none (no importa el estado del juego)
        if (passer_kicked_) {
            current_state_ = AgentState::IDLE;
            return Action::none();
        }
        
        // Durante kickoff: ir por el balón y patearlo
        const auto& ball = sensors.ball;
        
        if (!ball.visible) {
            return search_ball();
        }
        
        if (ball.distance > GameConfig::KICKABLE_DISTANCE) {
            return approach_ball(ball);
        }
        
        // Tiene el balón - hacer kickoff suave y marcar como hecho
        passer_kicked_ = true;
        return Action::kick(30, 0);  // Kickoff suave
    }
    
    Action decide_receiver(const SensorData& sensors) {
        const auto& ball = sensors.ball;
        const auto& goal = sensors.goal;
        
        // RECEIVER debe esperar hasta que el juego esté en PLAYING
        // (señal play_on del referee, que ocurre después del kickoff)
        // Durante BEFORE_KICK_OFF, el receiver NO debe moverse
        if (sensors.status != GameStatus::PLAYING) {
            current_state_ = AgentState::IDLE;
            return Action::none();
        }
        
        // Buscar balón si no es visible
        if (!ball.visible) {
            return search_ball();
        }
        
        // Ir hacia el balón si está lejos
        if (ball.distance > GameConfig::KICKABLE_DISTANCE) {
            return approach_ball(ball);
        }
        
        // Tiene el balón - disparar si ve el gol
        if (goal.visible && goal.distance < GameConfig::SHOOTING_DISTANCE) {
            return shoot_to_goal(goal);
        }
        
        // No ve el gol - driblear hacia él
        if (goal.visible) {
            current_state_ = AgentState::DRIBBLING;
            return Action::kick(30, goal.angle);
        }
        
        // Sin gol visible - usar triangulación o driblar hacia adelante
        return dribble_forward(sensors);
    }
    
    /**
     * @brief Goalkeeper SIMPLIFICADO para simulación.
     * - Turn inicial para mirar hacia el centro
     * - SIN movimiento después del turn
     * - Envía EXACTAMENTE UN catch cuando balón está a ≤2m
     */
    Action decide_goalkeeper(const SensorData& sensors) {
        const auto& ball = sensors.ball;
        
        // Si ya atrapó, no hacer nada más (evita penalty por múltiples catches)
        if (goalkeeper_caught_) {
            return Action::none();
        }
        
        // Turn inicial para mirar hacia el centro (una sola vez)
        if (!goalkeeper_turned_) {
            goalkeeper_turned_ = true;
            // Girar 180 grados para mirar hacia el centro de la cancha
            return Action::turn(180);
        }
        
        // Si no ve el balón, solo esperar (sin movimiento)
        if (!ball.visible) {
            return Action::none();
        }
        
        // Si el balón está a ≤3m (distancia aumentada para simulación), atrapar UNA VEZ
        if (ball.distance <= GameConfig::CATCHABLE_DISTANCE_GK_SIM) {
            goalkeeper_caught_ = true;  // Marcar como atrapado
            current_state_ = AgentState::CATCHING;
            return Action::catch_ball(ball.angle);
        }
        
        // No moverse, solo esperar
        return Action::none();
    }
    
    /**
     * @brief Striker SIMPLIFICADO para simulación de goalkeeper.
     * - SIN turn (para no desorientar los kicks)
     * - Dash hacia adelante si no ve la bola
     * - SIEMPRE patear hacia adelante (ángulo 0) con fuerza moderada
     */
    Action decide_striker_gk_sim(const SensorData& sensors) {
        const auto& ball = sensors.ball;
        
        // Si no ve la bola, dash hacia adelante (NO turn, para mantener orientación)
        if (!ball.visible) {
            current_state_ = AgentState::APPROACHING_BALL;
            return Action::dash(80, 0);  // Dash hacia adelante
        }
        
        // Si tiene la bola, SIEMPRE patear hacia adelante (ángulo 0) con fuerza SUAVE
        if (ball.distance <= GameConfig::KICKABLE_DISTANCE) {
            current_state_ = AgentState::SHOOTING;
            return Action::kick(30, 0);  // Fuerza suave para que el arquero pueda atrapar
        }
        
        // Acercarse a la bola: dash hacia el ángulo de la bola
        // Potencia MODERADA para no atravesar la bola
        current_state_ = AgentState::APPROACHING_BALL;
        float power = (ball.distance > 3.0f) ? 80.0f : 40.0f;
        return Action::dash(power, ball.angle);
    }
    
    Action decide_defender(const SensorData& sensors) {
        const auto& ball = sensors.ball;
        
        if (!ball.visible) {
            return search_ball();
        }
        
        // Si tiene el balón en rango de pateo, NO HACER NADA
        // Esto permite que el Striker se acerque y lo robe
        if (ball.distance < GameConfig::KICKABLE_DISTANCE) {
            current_state_ = AgentState::DEFENDING;
            return Action::none();  // Quedarse quieto con el balón
        }
        
        // Acercarse al balón (especialmente útil después del kickoff)
        current_state_ = AgentState::DEFENDING;
        return Action::dash(80, ball.angle);
    }
    
    // ========== KICKOFF ==========
    
    /**
     * @brief Kickoff handler SOLO para el PASSER.
     * El PASSER busca la pelota, se acerca a ella, y la patea para iniciar el juego.
     */
    Action handle_passer_kickoff(const SensorData& sensors) {
        const auto& ball = sensors.ball;
        
        // Si ya pateó, no hacer nada más
        if (passer_kicked_) {
            current_state_ = AgentState::IDLE;
            return Action::none();
        }
        
        if (!ball.visible) {
            current_state_ = AgentState::SEARCHING_BALL;
            return Action::turn(30);
        }
        
        // Si está en rango de pateo, patear para iniciar juego
        if (ball.distance <= GameConfig::KICKABLE_DISTANCE) {
            passer_kicked_ = true;  // Marcar que ya hizo kickoff
            current_state_ = AgentState::PASSING;
            return Action::kick(40, 0);  // Kickoff hacia adelante
        }
        
        // Dash progresivo: más agresivo pero frenando cerca
        current_state_ = AgentState::APPROACHING_BALL;
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

