set(ARDUINO_GFX_URL https://github.com/moononournation/Arduino_GFX.git)
set(ARDUINO_GFX_DIR ${CMAKE_CURRENT_BINARY_DIR}/Arduino_GFX)
if(NOT EXISTS ${ARDUINO_GFX_DIR})
  execute_process(COMMAND git clone --depth 1 ${ARDUINO_GFX_URL} ${ARDUINO_GFX_DIR})
endif()
file(GLOB_RECURSE ARDUINO_GFX_SOURCES ${ARDUINO_GFX_DIR}/src/*.c* )
idf_component_register(SRCS ${ARDUINO_GFX_SOURCES}
                      INCLUDE_DIRS ${ARDUINO_GFX_DIR}/src
                      REQUIRES arduino-esp32)
