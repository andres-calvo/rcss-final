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

Abre  localhost:5001 en tu navegador.

### 4. Iniciar Agente PC (opcional)

```bash
cd build
./platform-pc/agent_pc tcp://localhost:1883 ESP_01
```

> **Argumentos:**
> - `tcp://localhost:1883` - URL del broker MQTT
> - `ESP_01` - ID del dispositivo (debe coincidir con el configurado en el frontend)

### 5. Firmware ESP32

```bash
# Cargar entorno ESP-IDF
source /home/andres/esp/v5.5.1/esp-idf/export.sh

cd platform-esp32

# Build
idf.py build

# Flash (ajusta el puerto según tu sistema)
idf.py -p /dev/ttyACM0 flash

# Build + Flash + Monitor
idf.py -p /dev/ttyACM0 flash monitor
```

> **Nota:** Si tienes problemas de permisos con el puerto serial:
> ```bash
> sudo usermod -a -G dialout $USER
> # Cierra sesión y vuelve a entrar para aplicar cambios
> ```

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
