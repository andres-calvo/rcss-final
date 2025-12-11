cmake_minimum_required(VERSION 3.5)

# Incluir el common-cpp como componente
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../common-cpp")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(robocup_agent)
