# RoboCup Hybrid Simulation System

Sistema de simulación híbrida para RoboCup B-Division.

## Quick Start

### 1. Iniciar Infraestructura (MQTT)

```bash
docker-compose up -d
```

### 2. Iniciar Backend Python

```bash
cd backend-python
source venv/bin/activate
python -m src.app
```

### 3. Abrir Frontend

Abre `frontend/index.html` en tu navegador.

### 4. Iniciar Agente PC (opcional)

```bash
cd build
./platform-pc/agent_pc
```

## Estructura del Proyecto

```
rcss-final/
├── backend-python/     # Orquestador Flask+MQTT
├── common-cpp/         # Lógica compartida C++
├── platform-pc/        # Agente para PC
├── platform-esp32/     # Firmware ESP32
├── frontend/           # Panel de control web
└── tests-e2e/          # Pruebas de integración
```

## Tests

### Python
```bash
cd backend-python
source venv/bin/activate
python -m pytest tests/ -v
```

### C++
```bash
./build/tests/test_game_logic
```

## Escenarios Disponibles

- **Striker**: Buscar balón y disparar a gol
- **Dribbling**: Conducir balón de un lado a otro
- **Passing**: Pase coordinado entre dos jugadores
- **Goalkeeper**: Defender arco y atajar disparos
- **Defense**: Interceptar atacante sin cometer falta
