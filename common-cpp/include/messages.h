#ifndef ROBOCUP_MESSAGES_H
#define ROBOCUP_MESSAGES_H

/**
 * @file messages.h
 * @brief Estructuras de datos compartidas para comunicación entre componentes.
 * 
 * Este archivo define las estructuras que son comunes entre la versión PC
 * y la versión ESP32 del agente. No tiene dependencias de sistema operativo.
 */

#include <cstdint>

namespace robocup {

/**
 * @brief Estados posibles del juego.
 */
enum class GameStatus : uint8_t {
    IDLE = 0,
    BEFORE_KICK_OFF = 1,
    PLAYING = 2,
    FINISHED = 3
};

/**
 * @brief Roles disponibles para jugadores.
 */
enum class PlayerRole : uint8_t {
    STRIKER = 0,
    DRIBBLER = 1,
    PASSER = 2,
    RECEIVER = 3,
    GOALKEEPER = 4,
    DEFENDER = 5
};

/**
 * @brief Tipos de acciones que el agente puede ejecutar.
 */
enum class ActionType : uint8_t {
    NONE = 0,
    DASH = 1,     // Moverse: params[0]=potencia, params[1]=dirección
    TURN = 2,     // Girar: params[0]=ángulo
    KICK = 3,     // Patear: params[0]=potencia, params[1]=dirección
    CATCH = 4,    // Atrapar (goalkeeper): params[0]=dirección
    MOVE = 5      // Posicionar: params[0]=x, params[1]=y
};

/**
 * @brief Información de un objeto relativo al jugador.
 */
struct ObjectInfo {
    float distance;  // Distancia en metros
    float angle;     // Ángulo en grados (-180 a 180)
    bool visible;    // Si el objeto es visible
    
    ObjectInfo() : distance(0), angle(0), visible(false) {}
    ObjectInfo(float d, float a) : distance(d), angle(a), visible(true) {}
};

/**
 * @brief Información de un compañero de equipo.
 */
struct TeammateInfo {
    uint8_t player_id;
    float distance;
    float angle;
    bool visible;
    
    TeammateInfo() : player_id(0), distance(0), angle(0), visible(false) {}
    TeammateInfo(uint8_t id, float d, float a, bool v = true) 
        : player_id(id), distance(d), angle(a), visible(v) {}
};

/**
 * @brief Información de una bandera visible para triangulación.
 * 
 * Las banderas son puntos de referencia estáticos en el campo con posiciones
 * conocidas. Se usan para calcular la posición y orientación del jugador.
 */
struct FlagInfo {
    char name[16];     // "f c", "f l t", "g r", etc.
    float distance;
    float angle;
    bool visible;
    
    FlagInfo() : name{0}, distance(0), angle(0), visible(false) {}
    FlagInfo(const char* n, float d, float a) : distance(d), angle(a), visible(true) {
        // Copiar el nombre de forma segura
        for (int i = 0; i < 15 && n[i] != '\0'; ++i) {
            name[i] = n[i];
        }
        name[15] = '\0';
    }
};

/**
 * @brief Posición estimada del jugador en coordenadas absolutas.
 * 
 * Calculada mediante triangulación usando banderas visibles.
 */
struct PlayerPosition {
    float x;           // Posición X absoluta (-52.5 a 52.5)
    float y;           // Posición Y absoluta (-34 a 34)  
    float heading;     // Orientación absoluta (-180 a 180, 0 = hacia +X/derecha)
    bool valid;        // Si la estimación es confiable
    
    PlayerPosition() : x(0), y(0), heading(0), valid(false) {}
    PlayerPosition(float px, float py, float h) : x(px), y(py), heading(h), valid(true) {}
};

/**
 * @brief Datos de sensores recibidos del backend.
 * 
 * Esta estructura representa el estado del mundo desde la perspectiva
 * del jugador, tal como lo envía el backend Python via MQTT.
 */
struct SensorData {
    GameStatus status;
    PlayerRole role;
    
    ObjectInfo ball;
    ObjectInfo goal;
    
    static constexpr uint8_t MAX_TEAMMATES = 10;
    TeammateInfo teammates[MAX_TEAMMATES];
    uint8_t teammate_count;
    
    // Banderas para triangulación
    static constexpr uint8_t MAX_FLAGS = 10;
    FlagInfo flags[MAX_FLAGS];
    uint8_t flag_count;
    
    // Posición estimada del jugador
    PlayerPosition position;
    
    // Información adicional del jugador
    float stamina;
    float speed;
    
    SensorData() 
        : status(GameStatus::IDLE)
        , role(PlayerRole::STRIKER)
        , teammate_count(0)
        , flag_count(0)
        , stamina(8000)
        , speed(0) {}
};

/**
 * @brief Acción a ejecutar en el simulador.
 * 
 * Esta estructura se envía al backend Python para ser convertida
 * en comandos RCSSServer.
 */
struct Action {
    ActionType type;
    float params[2];
    
    Action() : type(ActionType::NONE), params{0, 0} {}
    
    static Action none() {
        return Action();
    }
    
    static Action dash(float power, float direction = 0) {
        Action a;
        a.type = ActionType::DASH;
        a.params[0] = power;
        a.params[1] = direction;
        return a;
    }
    
    static Action turn(float angle) {
        Action a;
        a.type = ActionType::TURN;
        a.params[0] = angle;
        return a;
    }
    
    static Action kick(float power, float direction) {
        Action a;
        a.type = ActionType::KICK;
        a.params[0] = power;
        a.params[1] = direction;
        return a;
    }
    
    static Action catch_ball(float direction) {
        Action a;
        a.type = ActionType::CATCH;
        a.params[0] = direction;
        return a;
    }
    
    static Action move(float x, float y) {
        Action a;
        a.type = ActionType::MOVE;
        a.params[0] = x;
        a.params[1] = y;
        return a;
    }
};

/**
 * @brief Mensaje de comunicación entre agentes del equipo.
 */
struct TeamMessage {
    uint8_t sender_id;
    char message[16];
    float target_x;
    float target_y;
    
    TeamMessage() : sender_id(0), message{0}, target_x(0), target_y(0) {}
};

} // namespace robocup

#endif // ROBOCUP_MESSAGES_H
