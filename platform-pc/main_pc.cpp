/**
 * @file main_pc.cpp
 * @brief Agente RoboCup para PC - Wrapper de pruebas en computadora.
 * 
 * Este programa simula un agente ESP32 en la PC para:
 * - Pruebas de integración sin hardware
 * - Desarrollo y debugging de la lógica
 * - Tests E2E con el backend Python
 */

#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>

#include "game_logic.h"
#include "messages.h"
#include "localization.h"

#if HAS_PAHO_MQTT
#include <mqtt/async_client.h>
#endif

namespace {
    std::atomic<bool> running{true};
    
    void signal_handler(int signal) {
        std::cout << "\nReceived signal " << signal << ", shutting down...\n";
        running = false;
    }
}

// =============================================================================
// Simulador simple sin MQTT (para pruebas unitarias)
// =============================================================================

void run_simple_simulation() {
    using namespace robocup;
    
    std::cout << "Running simple simulation (no MQTT)...\n";
    
    GameLogic logic;
    SensorData sensors;
    
    // Simular un escenario STRIKER
    sensors.status = GameStatus::PLAYING;
    sensors.role = PlayerRole::STRIKER;
    
    int cycle = 0;
    while (running && cycle < 100) {
        // Simular visión de bola
        if (cycle < 20) {
            // Buscando bola
            sensors.ball.visible = false;
        } else if (cycle < 50) {
            // Bola visible, lejos
            sensors.ball = ObjectInfo(15.0f - (cycle - 20) * 0.4f, 10.0f);
        } else {
            // Bola cerca, gol visible
            sensors.ball = ObjectInfo(0.5f, 0.0f);
            sensors.goal = ObjectInfo(20.0f, 5.0f);
        }
        
        Action action = logic.decide_action(sensors);
        
        // Mostrar acción
        const char* action_names[] = {"NONE", "DASH", "TURN", "KICK", "CATCH", "MOVE"};
        const char* state_names[] = {"IDLE", "SEARCHING", "APPROACHING", "DRIBBLING", 
                                     "SHOOTING", "PASSING", "DEFENDING", "CATCHING"};
        
        std::cout << "Cycle " << cycle 
                  << " | State: " << state_names[static_cast<int>(logic.get_state())]
                  << " | Action: " << action_names[static_cast<int>(action.type)]
                  << " (" << action.params[0] << ", " << action.params[1] << ")\n";
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        cycle++;
    }
    
    std::cout << "Simulation complete.\n";
}

#if HAS_PAHO_MQTT
// =============================================================================
// Cliente MQTT completo
// =============================================================================

class MQTTAgent {
public:
    MQTTAgent(const std::string& broker_address, const std::string& device_id)
        : client_(broker_address, device_id)
        , device_id_(device_id)
        , state_topic_("game/state/" + device_id)
        , action_topic_("player/action/" + device_id)
    {
    }
    
    bool connect() {
        try {
            mqtt::connect_options conn_opts;
            conn_opts.set_clean_session(true);
            
            std::cout << "Connecting to MQTT broker...\n";
            client_.connect(conn_opts)->wait();
            
            // Suscribirse al tópico de estado
            client_.subscribe(state_topic_, 1)->wait();
            std::cout << "Connected and subscribed to " << state_topic_ << "\n";
            
            // Iniciar el consumidor para poder usar try_consume_message_for
            client_.start_consuming();
            
            return true;
        } catch (const mqtt::exception& e) {
            std::cerr << "MQTT connection error: " << e.what() << "\n";
            return false;
        }
    }
    
    void run() {
        using namespace robocup;
        
        GameLogic logic;
        auto last_send_time = std::chrono::steady_clock::now();
        constexpr auto MIN_SEND_INTERVAL = std::chrono::milliseconds(75);  // 75ms rate limit
        
        while (running) {
            try {
                // Esperar mensaje de estado
                auto msg = client_.try_consume_message_for(std::chrono::milliseconds(50));
                
                if (msg) {
                    // Parsear JSON (simplificado)
                    std::string payload = msg->get_payload_str();
                    SensorData sensors = parse_sensors(payload);
                    
                    // Verificar rate limit (100ms entre comandos)
                    auto now = std::chrono::steady_clock::now();
                    if (now - last_send_time < MIN_SEND_INTERVAL) {
                        continue;  // Esperar más tiempo antes de enviar
                    }
                    
                    // Decidir acción
                    Action action = logic.decide_action(sensors);
                    
                    // Si es kick pero la bola está fuera de rango, convertir a dash
                    if (action.type == ActionType::KICK) {
                        if (!sensors.ball.visible || sensors.ball.distance > 0.8f) {
                            // Convertir kick inválido a dash hacia la bola
                            action.type = ActionType::DASH;
                            action.params[0] = 80.0f;  // Potencia
                            action.params[1] = sensors.ball.visible ? sensors.ball.angle : 0;
                        }
                    }
                    
                    // Enviar acción
                    if (action.type != ActionType::NONE) {
                        std::string action_json = action_to_json(action);
                        client_.publish(action_topic_, action_json, 1, false);
                        last_send_time = now;
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << "\n";
            }
        }
        
        client_.disconnect()->wait();
    }
    
private:
    mqtt::async_client client_;
    std::string device_id_;
    std::string state_topic_;
    std::string action_topic_;
    
    // Parser JSON simplificado (en producción usar nlohmann/json)
    robocup::SensorData parse_sensors(const std::string& json) {
        robocup::SensorData sensors;
        
        // Parseo muy básico de status
        if (json.find("\"PLAYING\"") != std::string::npos || 
            json.find("\"play_on\"") != std::string::npos) {
            sensors.status = robocup::GameStatus::PLAYING;
        } else if (json.find("\"BEFORE_KICK_OFF\"") != std::string::npos ||
                   json.find("\"before_kick_off\"") != std::string::npos ||
                   json.find("\"kick_off_l\"") != std::string::npos ||
                   json.find("\"kick_off_r\"") != std::string::npos) {
            sensors.status = robocup::GameStatus::BEFORE_KICK_OFF;
        } else if (json.find("\"FINISHED\"") != std::string::npos) {
            sensors.status = robocup::GameStatus::FINISHED;
        }
        
        // IMPORTANTE: STRIKER_GK_SIM debe ir ANTES de STRIKER porque contiene "STRIKER"
        if (json.find("\"STRIKER_GK_SIM\"") != std::string::npos) {
            sensors.role = robocup::PlayerRole::STRIKER_GK_SIM;
        } else if (json.find("\"STRIKER\"") != std::string::npos) {
            sensors.role = robocup::PlayerRole::STRIKER;
        } else if (json.find("\"GOALKEEPER\"") != std::string::npos) {
            sensors.role = robocup::PlayerRole::GOALKEEPER;
        } else if (json.find("\"DRIBBLER\"") != std::string::npos) {
            sensors.role = robocup::PlayerRole::DRIBBLER;
        } else if (json.find("\"DEFENDER\"") != std::string::npos) {
            sensors.role = robocup::PlayerRole::DEFENDER;
        } else if (json.find("\"PASSER\"") != std::string::npos) {
            sensors.role = robocup::PlayerRole::PASSER;
        } else if (json.find("\"RECEIVER\"") != std::string::npos) {
            sensors.role = robocup::PlayerRole::RECEIVER;
        }
        
        // Parsear ball distance/angle
        size_t ball_pos = json.find("\"ball\"");
        if (ball_pos != std::string::npos) {
            size_t dist_pos = json.find("\"dist\"", ball_pos);
            size_t angle_pos = json.find("\"angle\"", ball_pos);
            if (dist_pos != std::string::npos && angle_pos != std::string::npos) {
                sensors.ball.visible = true;
                // Parseo simplificado - extraer números
                size_t colon = json.find(":", dist_pos);
                if (colon != std::string::npos) {
                    sensors.ball.distance = std::stof(json.substr(colon + 1, 10));
                }
                colon = json.find(":", angle_pos);
                if (colon != std::string::npos) {
                    sensors.ball.angle = std::stof(json.substr(colon + 1, 10));
                }
            }
        }
        
        // Parsear goal distance/angle
        size_t goal_pos = json.find("\"goal\"");
        if (goal_pos != std::string::npos) {
            size_t dist_pos = json.find("\"dist\"", goal_pos);
            size_t angle_pos = json.find("\"angle\"", goal_pos);
            if (dist_pos != std::string::npos && angle_pos != std::string::npos) {
                sensors.goal.visible = true;
                size_t colon = json.find(":", dist_pos);
                if (colon != std::string::npos) {
                    sensors.goal.distance = std::stof(json.substr(colon + 1, 10));
                }
                colon = json.find(":", angle_pos);
                if (colon != std::string::npos) {
                    sensors.goal.angle = std::stof(json.substr(colon + 1, 10));
                }
            }
        }
        
        // Parsear flags para triangulación
        sensors.flag_count = 0;
        size_t flags_pos = json.find("\"flags\"");
        if (flags_pos != std::string::npos) {
            // Buscar cada bandera en el array
            size_t search_start = flags_pos;
            while (sensors.flag_count < robocup::SensorData::MAX_FLAGS) {
                size_t name_pos = json.find("\"name\"", search_start);
                if (name_pos == std::string::npos || name_pos > json.find("]", flags_pos)) break;
                
                // Extraer nombre
                size_t name_start = json.find("\"", name_pos + 6) + 1;
                size_t name_end = json.find("\"", name_start);
                std::string name = json.substr(name_start, name_end - name_start);
                
                // Extraer dist y angle
                size_t dist_pos = json.find("\"dist\"", name_end);
                size_t angle_pos = json.find("\"angle\"", name_end);
                
                if (dist_pos != std::string::npos && angle_pos != std::string::npos) {
                    robocup::FlagInfo& flag = sensors.flags[sensors.flag_count];
                    
                    // Copiar nombre
                    for (size_t i = 0; i < 15 && i < name.size(); ++i) {
                        flag.name[i] = name[i];
                    }
                    flag.name[std::min(name.size(), (size_t)15)] = '\0';
                    
                    size_t colon = json.find(":", dist_pos);
                    if (colon != std::string::npos) {
                        flag.distance = std::stof(json.substr(colon + 1, 10));
                    }
                    colon = json.find(":", angle_pos);
                    if (colon != std::string::npos) {
                        flag.angle = std::stof(json.substr(colon + 1, 10));
                    }
                    flag.visible = true;
                    sensors.flag_count++;
                }
                
                search_start = angle_pos + 1;
            }
        }
        
        // Calcular posición usando triangulación si hay suficientes banderas
        if (sensors.flag_count >= 2) {
            sensors.position = robocup::Localization::estimate_position(
                sensors.flags, sensors.flag_count);
        }
        
        return sensors;
    }
    
    std::string action_to_json(const robocup::Action& action) {
        const char* action_names[] = {"none", "dash", "turn", "kick", "catch", "move"};
        
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer),
            "{\"action\":\"%s\",\"params\":[%.1f,%.1f]}",
            action_names[static_cast<int>(action.type)],
            action.params[0], action.params[1]);
        
        return std::string(buffer);
    }
};

void run_mqtt_agent(const std::string& broker, const std::string& device_id) {
    MQTTAgent agent(broker, device_id);
    
    if (!agent.connect()) {
        std::cerr << "Failed to connect to MQTT broker\n";
        return;
    }
    
    agent.run();
}
#endif

// =============================================================================
// Main
// =============================================================================

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    std::cout << "=== RoboCup Agent (PC Platform) ===\n";
    
#if HAS_PAHO_MQTT
    std::string broker = "tcp://localhost:1883";
    std::string device_id = "ESP_01";
    
    if (argc > 1) {
        broker = argv[1];
    }
    if (argc > 2) {
        device_id = argv[2];
    }
    
    std::cout << "MQTT Broker: " << broker << "\n";
    std::cout << "Device ID: " << device_id << "\n\n";
    
    run_mqtt_agent(broker, device_id);
#else
    std::cout << "Built without MQTT support, running simple simulation\n\n";
    run_simple_simulation();
#endif
    
    return 0;
}
