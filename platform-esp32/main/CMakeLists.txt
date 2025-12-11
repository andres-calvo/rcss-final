idf_component_register(
    SRCS "main_esp32.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_wifi
        esp_event
        mqtt
        json
)

# Incluir headers del common-cpp
target_include_directories(${COMPONENT_LIB} PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}/../../common-cpp/include"
)
