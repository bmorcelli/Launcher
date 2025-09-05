set(ROTARY_URL https://github.com/mathertel/RotaryEncoder.git)
set(ROTARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/RotaryEncoder)
if(NOT EXISTS ${ROTARY_DIR})
  execute_process(COMMAND git clone --depth 1 ${ROTARY_URL} ${ROTARY_DIR})
endif()
file(GLOB ROTARY_SOURCES ${ROTARY_DIR}/src/*.cpp)
idf_component_register(SRCS ${ROTARY_SOURCES}
                      INCLUDE_DIRS ${ROTARY_DIR}/src
                      REQUIRES arduino-esp32)
