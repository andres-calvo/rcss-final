# Plan Maestro de Desarrollo: Sistema de Simulación Híbrida RoboCup (B-Division)

**Versión:** 1.0
**Enfoque:** TDD (Test Driven Development), Arquitectura Cross-Platform (C++), Hardware-in-the-Loop Simulado.

---

## 1. Visión General del Proyecto

El objetivo es desarrollar un sistema middleware que permita controlar jugadores en el simulador **RoboCup Soccer Server 2D (RCSSServer)** utilizando lógica de agentes escrita en **C++** destinada a microcontroladores **ESP32**.

El sistema actúa como un puente bidireccional:
1.  **Simulación Física:** RCSSServer maneja la física.
2.  **Orquestación:** Python traduce la visión compleja del simulador a datos de sensores simplificados (JSON).
3.  **Inteligencia:** El código C++ (común para PC y ESP32) toma decisiones de juego basándose únicamente en esos datos JSON.

---

## 2. Arquitectura del Sistema

### 2.1. Nodos Principales

| Componente | Tecnología | Responsabilidad |
| :--- | :--- | :--- |
| **Backend Orquestador** | Python 3 (Flask + SocketIO) | Gestionar ciclo de vida, puente UDP<->MQTT, API WebSocket para UI. |
| **Agente Inteligente** | C++ (Common Core) | Lógica de decisión pura (Matemáticas, FSM). **Agnóstico a la plataforma.** |
| **Plataforma PC** | C++ (std::thread) | Wrapper para correr el agente en PC (Linux/Win) durante tests E2E. |
| **Plataforma MCU** | C++ (ESP-IDF + FreeRTOS) | Wrapper para correr el agente en hardware real (ESP32). |
| **Cliente Web** | React JS | Panel de control en tiempo real (Start/Stop/Visualización). |
| **Infraestructura** | Mosquitto (MQTT) + RCSSServer | Broker de mensajería y Motor de físicas. |

### 2.2. Diagrama de Flujo de Datos

```mermaid
graph TD
    UI[Cliente Web React] -- WebSocket --> PY[Python Backend]
    RCSS[RCSSServer Simulator] -- UDP Vision --> PY
    PY -- UDP Commands --> RCSS
    
    PY -- MQTT (JSON Sensors) --> BROKER[Mosquitto]
    BROKER -- MQTT (JSON Actions) --> PY
    
    subgraph "Agente C++ (Cross-Platform)"
        BROKER <--> HAL[Capa de Abstracción (PC o ESP32)]
        HAL <--> LOGIC[Núcleo Lógico (Common C++)]
    end

3. Estructura de Directorios

Esta estructura es estricta para permitir TDD y compilación cruzada.

/robocup-system
├── /backend-python                # ORQUESTADOR
│   ├── /src
│   │   ├── domain.py              # Lógica de simulación (Objetivos)
│   │   ├── rcss_adapter.py        # Parser S-Expressions
│   │   ├── infrastructure/        # Flask, SocketIO, MQTT Client
│   │   └── app.py                 # Entry point
│   └── /tests                     # Tests Unitarios Python
│
├── /common-cpp                    # LÓGICA PURA (Library)
│   ├── /include
│   │   ├── game_logic.h           # Declaración de la IA
│   │   └── messages.h             # Structs (GameState, Action)
│   └── /src
│       └── game_logic.cpp         # Implementación (Sin deps de OS)
│
├── /platform-pc                   # SIMULADOR (Agente en PC)
│   ├── main_pc.cpp                # std::thread + Paho MQTT C++
│   └── CMakeLists.txt
│
├── /platform-esp32                # FIRMWARE (Agente en ESP32)
│   ├── /main
│   │   └── main_esp32.cpp         # FreeRTOS Tasks + ESP-MQTT
│   └── CMakeLists.txt
│
├── /tests-e2e                     # PRUEBAS DE SISTEMA
│   └── run_full_simulation.py     # Script que levanta todo el entorno
│
└── CMakeLists.txt                 # Build system raíz

4. Protocolos de Comunicación
4.1. MQTT (Data Plane - Tiempo Real)

El Backend publica en game/state, el Agente se suscribe. El Agente publica en player/action, el Backend se suscribe.

A. Sensores (Backend -> Agente): Tópico: game/state/{device_id}

{
  "status": "PLAYING",   // o "FINISHED", "IDLE"
  "role": "STRIKER",     // Rol asignado para la lógica
  "sensors": {
    "ball": { "dist": 15.5, "angle": -10.0 },
    "goal": { "dist": 40.0, "angle": 0.0 },
    "teammates": [{ "id": 2, "dist": 5.0, "angle": 20 }]
  }
}

B. Acciones (Agente -> Backend): Tópico: player/action/{device_id}

{
  "action": "dash",      // Comandos: dash, turn, kick, catch, move
  "params": [100, 30]    // [Potencia, Dirección] o [X, Y]
}

C. Comunicación Equipo (Agente <-> Agente): Tópico: team/comm

{
  "sender": "ESP_01",
  "msg": "PASSING",
  "target_coords": [10, 10]
}

4.2. WebSocket (Control Plane)

Puerto: 5001 (Namespace /).

    Request: simulation/start -> Payload: {"type": "dribbling", "roles": {...}}

    Request: simulation/stop -> Payload: {}

    Event: game/status -> Payload: {"state": "RUNNING", "score": "0-0", "time": 15}

    Event: system/log -> Payload: {"msg": "ESP32 conectada", "level": "INFO"}


5. Especificación de Escenarios (Entregables)

El sistema debe implementar 5 simulaciones (seleccionables vía Web):

    Dribbling: Un agente (Rol: DRIBBLER) conduce el balón de un lado a otro.

    Striker: Un agente (Rol: STRIKER) busca el balón en posición aleatoria y dispara a gol.

    Pases: Dos agentes (Rol: PASSER, RECEIVER) coordinan un pase y gol usando MQTT team/comm.

    Goalkeeper: Un agente (Rol: GOALKEEPER) defiende el arco de un bot rival. Debe atajar (catch) y despejar.

    Defensa: Un agente (Rol: DEFENDER) intercepta a un atacante sin colisionar (foul).

Lógica de Terminación: Si se cumple el objetivo o hay timeout, Python envía status: "FINISHED". El firmware C++ debe detectar esto, detener motores y reiniciar su máquina de estados a IDLE.

6. Estrategia de Desarrollo (Metodología)
Fase 1: Infraestructura y Backend (TDD Python)

    Objetivo: Tener el servidor capaz de hablar con RCSSServer y publicar en MQTT.

    Tests:

        Unitarios: Parser de S-Expressions (Regex de visión).

        Integración: Ciclo de vida de procesos (Iniciar/Matar rcssserver).

Fase 2: Núcleo Lógico C++ (TDD Host)

    Objetivo: Crear la librería /common-cpp sin dependencias de hardware.

    Herramienta: Unity o GoogleTest corriendo en PC.

    Tests:

        test_game_logic.cpp: Verificar que dados X datos de sensor, la función decide_action retorna el comando correcto.

Fase 3: Integración E2E (Simulación PC)

    Objetivo: Conectar el Backend con el Agente C++ corriendo como proceso en Windows/Linux.

    Tests:

        run_full_simulation.py: Script automatizado que valida flujo completo: Web Start -> Gol -> Web Finish.

Fase 4: Portado a Hardware (ESP32)

    Objetivo: Compilar /platform-esp32.

    Acción: Reemplazar std::thread por xTaskCreate y sockets por esp_mqtt.

    Validación: Flashear y ver que el comportamiento es idéntico al de la Fase 3.

Fase 5: Frontend

    Objetivo: Interfaz gráfica React.

    Acción: Conectar a SocketIO y maquetar Dashboard.

7. Prompt Maestro (Para Generación de Código)

Copia y pega el siguiente bloque cuando necesites que una IA genere código específico para este proyecto.

    System Context: Eres un Ingeniero de Software experto en Sistemas Embebidos (ESP32/FreeRTOS), C++ Moderno y Python (Flask). Estamos construyendo un sistema de simulación RoboCup híbrido.

    Restricciones Técnicas:

        Backend: Python Flask + SocketIO. Usa TDD (pytest).

        Firmware: C++ con arquitectura de "Núcleo Compartido". La lógica reside en common-cpp y es pura (sin dependencias de OS).

        Testing: Primero escribe el test (RED), luego el código (GREEN).

        Simulación: Usamos platform-pc para simular la ESP32 en la computadora antes de ir al hardware.

    Estructura de Archivos: Respeta estrictamente la separación: /backend-python, /common-cpp, /platform-pc, /platform-esp32.

    Tarea Actual: [INSERTA AQUÍ LA TAREA ESPECÍFICA, EJ: "Genera el parser de visión en Python con sus tests"]