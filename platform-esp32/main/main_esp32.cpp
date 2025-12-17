/**
 * @file main_esp32.cpp
 * @brief Agente RoboCup para ESP32 - Firmware con FreeRTOS.
 * 
 * Este firmware ejecuta la misma lógica que el agente PC pero
 * usando FreeRTOS para multitarea y ESP-MQTT para comunicación.
 */

#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "cJSON.h"

// Incluir lógica compartida
#include "game_logic.h"
#include "messages.h"

static const char* TAG = "ROBOCUP_AGENT";

// =============================================================================
// Configuración
// =============================================================================

#define WIFI_SSID       CONFIG_ESP_WIFI_SSID
#define WIFI_PASS       CONFIG_ESP_WIFI_PASSWORD
#define MQTT_BROKER     CONFIG_MQTT_BROKER_URL
#define DEVICE_ID       "ESP_01"

// Topics MQTT
#define TOPIC_STATE     "game/state/" DEVICE_ID
#define TOPIC_ACTION    "player/action/" DEVICE_ID
#define TOPIC_TEAM      "team/comm"

// Rate limiting
#define MIN_SEND_INTERVAL_MS 75

// =============================================================================
// Variables globales
// =============================================================================

static EventGroupHandle_t wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;

static esp_mqtt_client_handle_t mqtt_client = nullptr;
static QueueHandle_t sensor_queue = nullptr;

static robocup::GameLogic game_logic;

// =============================================================================
// WiFi
// =============================================================================

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGI(TAG, "Disconnected, reconnecting...");
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init() {
    wifi_event_group = xEventGroupCreate();
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                   &wifi_event_handler, nullptr, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                   &wifi_event_handler, nullptr, &instance_got_ip));
    
    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi init complete, connecting to %s...", WIFI_SSID);
}

// =============================================================================
// MQTT
// =============================================================================

static robocup::SensorData parse_sensor_json(const char* json_str) {
    robocup::SensorData sensors;
    
    cJSON* root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse JSON");
        return sensors;
    }
    
    // Status
    cJSON* status = cJSON_GetObjectItem(root, "status");
    if (status && cJSON_IsString(status)) {
        if (strcmp(status->valuestring, "PLAYING") == 0 ||
            strcmp(status->valuestring, "play_on") == 0) {
            sensors.status = robocup::GameStatus::PLAYING;
        } else if (strcmp(status->valuestring, "BEFORE_KICK_OFF") == 0 ||
                   strcmp(status->valuestring, "before_kick_off") == 0 ||
                   strcmp(status->valuestring, "kick_off_l") == 0 ||
                   strcmp(status->valuestring, "kick_off_r") == 0) {
            sensors.status = robocup::GameStatus::BEFORE_KICK_OFF;
        } else if (strcmp(status->valuestring, "FINISHED") == 0) {
            sensors.status = robocup::GameStatus::FINISHED;
        }
    }
    
    // Role
    cJSON* role = cJSON_GetObjectItem(root, "role");
    if (role && cJSON_IsString(role)) {
        // IMPORTANTE: STRIKER_GK_SIM debe ir ANTES de STRIKER porque contiene "STRIKER"
        if (strcmp(role->valuestring, "STRIKER_GK_SIM") == 0) {
            sensors.role = robocup::PlayerRole::STRIKER_GK_SIM;
        } else if (strcmp(role->valuestring, "STRIKER") == 0) {
            sensors.role = robocup::PlayerRole::STRIKER;
        } else if (strcmp(role->valuestring, "GOALKEEPER") == 0) {
            sensors.role = robocup::PlayerRole::GOALKEEPER;
        } else if (strcmp(role->valuestring, "DRIBBLER") == 0) {
            sensors.role = robocup::PlayerRole::DRIBBLER;
        } else if (strcmp(role->valuestring, "DEFENDER") == 0) {
            sensors.role = robocup::PlayerRole::DEFENDER;
        } else if (strcmp(role->valuestring, "PASSER") == 0) {
            sensors.role = robocup::PlayerRole::PASSER;
        } else if (strcmp(role->valuestring, "RECEIVER") == 0) {
            sensors.role = robocup::PlayerRole::RECEIVER;
        }
    }
    
    // Sensors
    cJSON* sensor_obj = cJSON_GetObjectItem(root, "sensors");
    if (sensor_obj) {
        // Ball
        cJSON* ball = cJSON_GetObjectItem(sensor_obj, "ball");
        if (ball) {
            cJSON* dist = cJSON_GetObjectItem(ball, "dist");
            cJSON* angle = cJSON_GetObjectItem(ball, "angle");
            if (dist && angle) {
                sensors.ball.distance = (float)dist->valuedouble;
                sensors.ball.angle = (float)angle->valuedouble;
                sensors.ball.visible = true;
            }
        }
        
        // Goal
        cJSON* goal = cJSON_GetObjectItem(sensor_obj, "goal");
        if (goal) {
            cJSON* dist = cJSON_GetObjectItem(goal, "dist");
            cJSON* angle = cJSON_GetObjectItem(goal, "angle");
            if (dist && angle) {
                sensors.goal.distance = (float)dist->valuedouble;
                sensors.goal.angle = (float)angle->valuedouble;
                sensors.goal.visible = true;
            }
        }
    }
    
    cJSON_Delete(root);
    return sensors;
}

static void publish_action(const robocup::Action& action) {
    if (!mqtt_client) return;
    
    const char* action_names[] = {"none", "dash", "turn", "kick", "catch", "move"};
    
    char buffer[128];
    snprintf(buffer, sizeof(buffer),
        "{\"action\":\"%s\",\"params\":[%.1f,%.1f]}",
        action_names[static_cast<int>(action.type)],
        action.params[0], action.params[1]);
    
    esp_mqtt_client_publish(mqtt_client, TOPIC_ACTION, buffer, 0, 1, 0);
    ESP_LOGD(TAG, "Published: %s", buffer);
}

// Buffer estático para ensamblar mensajes fragmentados
static char mqtt_data_buffer[2048] = {0};
static int mqtt_data_offset = 0;
static char mqtt_topic_buffer[64] = {0};

static void mqtt_event_handler(void* args, esp_event_base_t base, 
                               int32_t event_id, void* event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    
    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            esp_mqtt_client_subscribe(mqtt_client, TOPIC_STATE, 1);
            esp_mqtt_client_subscribe(mqtt_client, TOPIC_TEAM, 1);
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            break;
            
        case MQTT_EVENT_DATA: {
            // Verificar si es el inicio de un nuevo mensaje
            if (event->current_data_offset == 0) {
                // Nuevo mensaje - guardar topic y resetear buffer
                mqtt_data_offset = 0;
                memset(mqtt_data_buffer, 0, sizeof(mqtt_data_buffer));
                memset(mqtt_topic_buffer, 0, sizeof(mqtt_topic_buffer));
                
                int topic_len = event->topic_len < 63 ? event->topic_len : 63;
                memcpy(mqtt_topic_buffer, event->topic, topic_len);
            }
            
            // Copiar fragmento al buffer
            int copy_len = event->data_len;
            if (mqtt_data_offset + copy_len >= sizeof(mqtt_data_buffer) - 1) {
                copy_len = sizeof(mqtt_data_buffer) - 1 - mqtt_data_offset;
                ESP_LOGW(TAG, "MQTT buffer overflow, truncating");
            }
            
            if (copy_len > 0) {
                memcpy(mqtt_data_buffer + mqtt_data_offset, event->data, copy_len);
                mqtt_data_offset += copy_len;
            }
            
            // Verificar si el mensaje está completo
            bool is_complete = (event->current_data_offset + event->data_len >= event->total_data_len);
            
            ESP_LOGD(TAG, "MQTT fragment: offset=%d, len=%d, total=%d, complete=%d",
                     event->current_data_offset, event->data_len, 
                     event->total_data_len, is_complete);
            
            if (is_complete) {
                ESP_LOGI(TAG, "MQTT complete message, topic: %s, total_len: %d", 
                         mqtt_topic_buffer, mqtt_data_offset);
                
                if (strstr(mqtt_topic_buffer, "game/state") != nullptr) {
                    robocup::SensorData sensors = parse_sensor_json(mqtt_data_buffer);
                    if (sensors.status != robocup::GameStatus::IDLE) {
                        ESP_LOGI(TAG, "Parsed - Status: %d, Role: %d, Ball visible: %d", 
                                 static_cast<int>(sensors.status),
                                 static_cast<int>(sensors.role),
                                 sensors.ball.visible);
                    }
                    xQueueSend(sensor_queue, &sensors, 0);
                }
                
                // Resetear para siguiente mensaje
                mqtt_data_offset = 0;
            }
            break;
        }
        
        default:
            break;
    }
}

static void mqtt_init() {
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = MQTT_BROKER;
    
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, 
                                   mqtt_event_handler, nullptr);
    esp_mqtt_client_start(mqtt_client);
    
    ESP_LOGI(TAG, "MQTT client started, broker: %s", MQTT_BROKER);
}

// =============================================================================
// Tarea principal del agente
// =============================================================================

static void agent_task(void* pvParameters) {
    ESP_LOGI(TAG, "Agent task started");
    
    robocup::SensorData sensors;
    TickType_t last_send_time = 0;
    
    while (true) {
        // Esperar datos de sensores del broker
        if (xQueueReceive(sensor_queue, &sensors, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Verificar rate limit (75ms entre comandos)
            TickType_t now = xTaskGetTickCount();
            // TODO: Analizar el uso de VtaskDelay 
            if ((now - last_send_time) < pdMS_TO_TICKS(MIN_SEND_INTERVAL_MS)) {
                continue;  // Esperar más tiempo antes de enviar
            }
            
            // Decidir acción
            robocup::Action action = game_logic.decide_action(sensors);
            
            // TODO: Esta logica deberia estar en el game logic, esto viene del platform-pc entonces ajusta alla tambien
            // Si es kick pero la bola está fuera de rango, convertir a dash
            if (action.type == robocup::ActionType::KICK) {
                if (!sensors.ball.visible || sensors.ball.distance > 0.8f) {
                    // Convertir kick inválido a dash hacia la bola
                    action.type = robocup::ActionType::DASH;
                    action.params[0] = 80.0f;  // Potencia
                    action.params[1] = sensors.ball.visible ? sensors.ball.angle : 0;
                }
            }
            
            // Publicar si no es NONE
            if (action.type != robocup::ActionType::NONE) {
                publish_action(action);
                last_send_time = now;
            }
            
            // Log de estado
            const char* state_names[] = {"IDLE", "SEARCHING", "APPROACHING", "DRIBBLING",
                                         "SHOOTING", "PASSING", "DEFENDING", "CATCHING"};
            ESP_LOGI(TAG, "State: %s", state_names[static_cast<int>(game_logic.get_state())]);
        }
        
        // Si el juego terminó, resetear
        if (sensors.status == robocup::GameStatus::FINISHED) {
            game_logic.reset();
            ESP_LOGI(TAG, "Game finished, agent reset");
        }
    }
}

// =============================================================================
// Entry Point
// =============================================================================

extern "C" void app_main() {
    ESP_LOGI(TAG, "=== RoboCup Agent ESP32 ===");
    ESP_LOGI(TAG, "Device ID: %s", DEVICE_ID);
    
    // Inicializar NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Crear cola para sensores
    sensor_queue = xQueueCreate(10, sizeof(robocup::SensorData));
    
    // Inicializar WiFi
    wifi_init();
    
    // Esperar conexión WiFi
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);
    
    // Inicializar MQTT
    mqtt_init();
    
    // Crear tarea del agente
    xTaskCreate(agent_task, "agent_task", 8192, nullptr, 5, nullptr);
    
    ESP_LOGI(TAG, "System initialized, agent running");
}
