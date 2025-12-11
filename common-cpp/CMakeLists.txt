cmake_minimum_required(VERSION 3.14)
project(robocup_common VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Esta es una librería header-only, así que creamos una interface library
add_library(robocup_common INTERFACE)
target_include_directories(robocup_common INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Alias para uso más limpio
add_library(robocup::common ALIAS robocup_common)
