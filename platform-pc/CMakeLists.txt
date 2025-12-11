cmake_minimum_required(VERSION 3.14)
project(robocup_platform_pc VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Buscar Paho MQTT C++ manualmente en /usr/local
find_library(PAHO_MQTT_C_LIB paho-mqtt3a PATHS /usr/local/lib)
find_library(PAHO_MQTT_CPP_LIB paho-mqttpp3 PATHS /usr/local/lib)
find_path(PAHO_MQTT_CPP_INCLUDE mqtt/async_client.h PATHS /usr/local/include)

add_executable(agent_pc main_pc.cpp)

# Enlazar con la librería común
target_link_libraries(agent_pc PRIVATE robocup::common)

# Si Paho MQTT está disponible, enlazar
if(PAHO_MQTT_CPP_LIB AND PAHO_MQTT_C_LIB AND PAHO_MQTT_CPP_INCLUDE)
    message(STATUS "Paho MQTT C++ found at ${PAHO_MQTT_CPP_LIB}")
    target_include_directories(agent_pc PRIVATE ${PAHO_MQTT_CPP_INCLUDE})
    target_link_libraries(agent_pc PRIVATE ${PAHO_MQTT_CPP_LIB} ${PAHO_MQTT_C_LIB})
    target_compile_definitions(agent_pc PRIVATE HAS_PAHO_MQTT=1)
else()
    message(STATUS "Paho MQTT C++ not found, building without MQTT support")
    target_compile_definitions(agent_pc PRIVATE HAS_PAHO_MQTT=0)
endif()

# Threads
find_package(Threads REQUIRED)
target_link_libraries(agent_pc PRIVATE Threads::Threads)
