# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.cpp ${CMAKE_SOURCE_DIR}/src/*.c)

# Gather sources from local PlatformIO libraries
set(PROJ_LIB_DIR ${CMAKE_SOURCE_DIR}/lib)
file(GLOB_RECURSE lib_sources
     ${PROJ_LIB_DIR}/Custom_Update/src/*.cpp
     ${PROJ_LIB_DIR}/M5Stack-HTTPUpdate/src/*.cpp
     ${PROJ_LIB_DIR}/CYD-touch/*.cpp
     ${PROJ_LIB_DIR}/utility/*.cpp)
list(APPEND app_sources ${lib_sources})

list(APPEND extra_includes
     ${PROJ_LIB_DIR}/Custom_Update/src
     ${PROJ_LIB_DIR}/M5Stack-HTTPUpdate/src
     ${PROJ_LIB_DIR}/CYD-touch
     ${PROJ_LIB_DIR}/utility)
if(PIO_BOARD_DIR)
  list(APPEND app_sources ${CMAKE_SOURCE_DIR}/boards/${PIO_BOARD_DIR}/interface.cpp)
  list(APPEND extra_includes ${CMAKE_SOURCE_DIR}/boards/${PIO_BOARD_DIR})
endif()

idf_component_register(SRCS ${app_sources}
                      INCLUDE_DIRS "." "../include" ${extra_includes}
                      REQUIRES arduino-esp32 ArduinoGFX RotaryEncoder XPowersLib AsyncTCP ESPAsyncWebServer ArduinoJson app_update wear_levelling)
