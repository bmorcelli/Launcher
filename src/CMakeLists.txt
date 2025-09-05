# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.cpp ${CMAKE_SOURCE_DIR}/src/*.c)
if(PIO_BOARD_DIR)
  list(APPEND app_sources ${CMAKE_SOURCE_DIR}/boards/${PIO_BOARD_DIR}/interface.cpp)
  list(APPEND extra_includes ${CMAKE_SOURCE_DIR}/boards/${PIO_BOARD_DIR})
endif()

idf_component_register(SRCS ${app_sources}
                      INCLUDE_DIRS "." "../include" ${extra_includes}
                      REQUIRES arduino-esp32 ArduinoGFX RotaryEncoder XPowersLib)
