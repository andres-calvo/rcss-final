cmake_minimum_required(VERSION 3.14)
project(robocup_system VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Opciones de build
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_PLATFORM_PC "Build PC platform agent" ON)

# Agregar subdirectorios
add_subdirectory(common-cpp)

if(BUILD_PLATFORM_PC)
    add_subdirectory(platform-pc)
endif()

# Tests con GoogleTest
if(BUILD_TESTS)
    enable_testing()
    
    # Descargar GoogleTest
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
    
    # Agregar tests
    add_subdirectory(tests)
endif()
