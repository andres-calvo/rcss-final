#ifndef ROBOCUP_LOCALIZATION_H
#define ROBOCUP_LOCALIZATION_H

/**
 * @file localization.h
 * @brief Sistema de triangulación usando banderas del campo.
 * 
 * Calcula la posición y orientación absoluta del jugador basándose
 * en las banderas visibles del rcssserver.
 */

#include "messages.h"
#include <cmath>
#include <cstring>

namespace robocup {

/**
 * @brief Clase estática para cálculos de localización.
 */
class Localization {
public:
    /**
     * @brief Estima la posición del jugador usando banderas visibles.
     * 
     * Algoritmo mejorado:
     * 1. Buscar todas las banderas con posiciones conocidas
     * 2. Usar múltiples pares para triangular y promediar posición
     * 3. Calcular heading promediando desde múltiples banderas
     */
    static PlayerPosition estimate_position(const FlagInfo* flags, uint8_t count) {
        if (count < 2) {
            return PlayerPosition();  // No válido
        }
        
        // Recopilar todas las banderas conocidas
        struct KnownFlagData {
            float x, y, dist, angle;
            bool valid;
        };
        KnownFlagData known_flags[10];
        uint8_t known_count = 0;
        
        for (uint8_t i = 0; i < count && known_count < 10; ++i) {
            if (!flags[i].visible) continue;
            
            float fx, fy;
            if (!get_flag_position(flags[i].name, fx, fy)) continue;
            
            known_flags[known_count].x = fx;
            known_flags[known_count].y = fy;
            known_flags[known_count].dist = flags[i].distance;
            known_flags[known_count].angle = flags[i].angle;
            known_flags[known_count].valid = true;
            known_count++;
        }
        
        if (known_count < 2) {
            return PlayerPosition();
        }
        
        // Triangular con el primer par de banderas
        PlayerPosition pos = triangulate(
            known_flags[0].x, known_flags[0].y, known_flags[0].dist,
            known_flags[1].x, known_flags[1].y, known_flags[1].dist);
        
        if (!pos.valid) {
            return PlayerPosition();
        }
        
        // Calcular heading usando TODAS las banderas conocidas (promedio)
        // heading = atan2(flag_y - player_y, flag_x - player_x) - angle_observado
        float heading_sum = 0;
        float sin_sum = 0, cos_sum = 0;  // Para promedio circular
        int heading_count = 0;
        
        for (uint8_t i = 0; i < known_count; ++i) {
            float dx = known_flags[i].x - pos.x;
            float dy = known_flags[i].y - pos.y;
            float angle_to_flag = atan2f(dy, dx) * 180.0f / 3.14159f;
            float heading = normalize_angle(angle_to_flag - known_flags[i].angle);
            
            // Usar promedio circular para evitar problemas con ángulos cerca de ±180
            float heading_rad = heading * 3.14159f / 180.0f;
            sin_sum += sinf(heading_rad);
            cos_sum += cosf(heading_rad);
            heading_count++;
        }
        
        if (heading_count > 0) {
            // Promedio circular
            pos.heading = atan2f(sin_sum, cos_sum) * 180.0f / 3.14159f;
        }
        
        return pos;
    }
    
    /**
     * @brief Calcula el ángulo relativo hacia un punto objetivo.
     * @return Ángulo que hay que girar para mirar al objetivo
     */
    static float angle_to_target(const PlayerPosition& pos, float target_x, float target_y) {
        if (!pos.valid) return 0;
        
        float angle_to_target = atan2f(target_y - pos.y, target_x - pos.x) * 180.0f / 3.14159f;
        return normalize_angle(angle_to_target - pos.heading);
    }
    
    /**
     * @brief Ángulo hacia el arco enemigo (derecho, x=52.5).
     */
    static float angle_to_enemy_goal(const PlayerPosition& pos) {
        return angle_to_target(pos, 52.5f, 0.0f);
    }

private:
    // Estructura para posiciones conocidas de banderas
    struct KnownFlag {
        const char* name;
        float x;
        float y;
    };
    
    // Mapa de banderas principales del campo
    // El campo mide 105x68 metros (de -52.5 a 52.5 en X, de -34 a 34 en Y)
    static constexpr int NUM_KNOWN_FLAGS = 20;
    
    /**
     * @brief Obtiene la posición conocida de una bandera por nombre.
     * @return true si la bandera es conocida
     */
    static bool get_flag_position(const char* name, float& x, float& y) {
        // Banderas principales - posiciones fijas del campo rcssserver
        // Campo: 105x68 metros, centro en (0,0)
        
        // Centro
        if (strcmp(name, "f c") == 0) { x = 0; y = 0; return true; }
        
        // Esquinas (5 metros afuera del campo)
        if (strcmp(name, "f l t") == 0) { x = -57.5f; y = 39.0f; return true; }
        if (strcmp(name, "f l b") == 0) { x = -57.5f; y = -39.0f; return true; }
        if (strcmp(name, "f r t") == 0) { x = 57.5f; y = 39.0f; return true; }
        if (strcmp(name, "f r b") == 0) { x = 57.5f; y = -39.0f; return true; }
        
        // Centros de líneas laterales (5 metros afuera)
        if (strcmp(name, "f c t") == 0) { x = 0; y = 39.0f; return true; }
        if (strcmp(name, "f c b") == 0) { x = 0; y = -39.0f; return true; }
        if (strcmp(name, "f l 0") == 0) { x = -57.5f; y = 0; return true; }
        if (strcmp(name, "f r 0") == 0) { x = 57.5f; y = 0; return true; }
        
        // Arcos
        if (strcmp(name, "g l") == 0) { x = -52.5f; y = 0; return true; }
        if (strcmp(name, "g r") == 0) { x = 52.5f; y = 0; return true; }
        
        // Postes de gol
        if (strcmp(name, "f g l t") == 0) { x = -52.5f; y = 7.01f; return true; }
        if (strcmp(name, "f g l b") == 0) { x = -52.5f; y = -7.01f; return true; }
        if (strcmp(name, "f g r t") == 0) { x = 52.5f; y = 7.01f; return true; }
        if (strcmp(name, "f g r b") == 0) { x = 52.5f; y = -7.01f; return true; }
        
        // Área penal
        if (strcmp(name, "f p l t") == 0) { x = -36.0f; y = 20.16f; return true; }
        if (strcmp(name, "f p l b") == 0) { x = -36.0f; y = -20.16f; return true; }
        if (strcmp(name, "f p l c") == 0) { x = -36.0f; y = 0; return true; }
        if (strcmp(name, "f p r t") == 0) { x = 36.0f; y = 20.16f; return true; }
        if (strcmp(name, "f p r b") == 0) { x = 36.0f; y = -20.16f; return true; }
        if (strcmp(name, "f p r c") == 0) { x = 36.0f; y = 0; return true; }
        
        // Banderas laterales con números (f t l N, f t r N, f b l N, f b r N)
        // Estas tienen formato como "f t l 10" - detectar y parsear
        if (strncmp(name, "f t l ", 6) == 0 || strncmp(name, "f t r ", 6) == 0 ||
            strncmp(name, "f b l ", 6) == 0 || strncmp(name, "f b r ", 6) == 0) {
            // Parsear el número
            int num = 0;
            const char* p = name + 6;
            while (*p >= '0' && *p <= '9') {
                num = num * 10 + (*p - '0');
                ++p;
            }
            
            if (name[2] == 't') {  // Top (y positivo)
                y = 39.0f;  // 5 metros afuera
            } else {  // Bottom 
                y = -39.0f;
            }
            
            if (name[4] == 'l') {  // Left (x negativo)
                x = (float)(-num);
            } else {  // Right
                x = (float)num;
            }
            return true;
        }
        
        // Banderas laterales izquierda/derecha con números
        if (strncmp(name, "f l t ", 6) == 0 || strncmp(name, "f l b ", 6) == 0 ||
            strncmp(name, "f r t ", 6) == 0 || strncmp(name, "f r b ", 6) == 0) {
            int num = 0;
            const char* p = name + 6;
            while (*p >= '0' && *p <= '9') {
                num = num * 10 + (*p - '0');
                ++p;
            }
            
            if (name[2] == 'l') {  // Left
                x = -57.5f;
            } else {  // Right
                x = 57.5f;
            }
            
            if (name[4] == 't') {  // Top (y positivo)
                y = (float)num;
            } else {  // Bottom
                y = (float)(-num);
            }
            return true;
        }
        
        return false;  // Bandera no conocida
    }
    
    /**
     * @brief Triangulación usando intersección de dos círculos.
     */
    static PlayerPosition triangulate(float x1, float y1, float r1,
                                       float x2, float y2, float r2) {
        // Distancia entre los centros
        float dx = x2 - x1;
        float dy = y2 - y1;
        float d = sqrtf(dx * dx + dy * dy);
        
        // Verificar si hay solución
        if (d > r1 + r2 || d < fabsf(r1 - r2) || d == 0) {
            return PlayerPosition();  // No hay intersección
        }
        
        // Calcular el punto de intersección
        // Fórmula de intersección de círculos
        float a = (r1 * r1 - r2 * r2 + d * d) / (2 * d);
        float h_sq = r1 * r1 - a * a;
        
        if (h_sq < 0) {
            return PlayerPosition();
        }
        
        float h = sqrtf(h_sq);
        
        // Punto medio en la línea entre centros
        float px = x1 + a * dx / d;
        float py = y1 + a * dy / d;
        
        // Dos posibles puntos de intersección
        float ix1 = px + h * dy / d;
        float iy1 = py - h * dx / d;
        float ix2 = px - h * dy / d;
        float iy2 = py + h * dx / d;
        
        // Elegir el punto que está dentro del campo (preferiblemente)
        // Campo: -52.5 to 52.5 en X, -34 to 34 en Y
        bool p1_in = (ix1 >= -55 && ix1 <= 55 && iy1 >= -37 && iy1 <= 37);
        bool p2_in = (ix2 >= -55 && ix2 <= 55 && iy2 >= -37 && iy2 <= 37);
        
        if (p1_in && !p2_in) {
            return PlayerPosition(ix1, iy1, 0);
        } else if (p2_in && !p1_in) {
            return PlayerPosition(ix2, iy2, 0);
        } else {
            // Ambos o ninguno dentro - usar el primero
            return PlayerPosition(ix1, iy1, 0);
        }
    }
    
    /**
     * @brief Normaliza un ángulo al rango [-180, 180].
     */
    static float normalize_angle(float angle) {
        while (angle > 180.0f) angle -= 360.0f;
        while (angle < -180.0f) angle += 360.0f;
        return angle;
    }
};

} // namespace robocup

#endif // ROBOCUP_LOCALIZATION_H
