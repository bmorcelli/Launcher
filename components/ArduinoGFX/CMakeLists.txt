set(ARDUINO_GFX_URL https://github.com/moononournation/Arduino_GFX.git)
set(ARDUINO_GFX_DIR ${CMAKE_CURRENT_BINARY_DIR}/Arduino_GFX)
if(NOT EXISTS ${ARDUINO_GFX_DIR})
  execute_process(COMMAND git clone --depth 1 ${ARDUINO_GFX_URL} ${ARDUINO_GFX_DIR})
endif()

# Apply local patches (e.g. narrowing fixes) once after cloning
set(NARROWING_FILE ${ARDUINO_GFX_DIR}/src/databus/Arduino_ESP32LCD8.cpp)
if(EXISTS ${NARROWING_FILE} AND NOT EXISTS ${ARDUINO_GFX_DIR}/.patched)
  file(READ ${NARROWING_FILE} SRC)
  string(REPLACE ".pclk_hz = _speed," ".pclk_hz = static_cast<uint32_t>(_speed)," SRC "${SRC}")
  file(WRITE ${NARROWING_FILE} "${SRC}")
  file(WRITE ${ARDUINO_GFX_DIR}/.patched "")
endif()

file(GLOB_RECURSE ARDUINO_GFX_SOURCES ${ARDUINO_GFX_DIR}/src/*.c* )
idf_component_register(SRCS ${ARDUINO_GFX_SOURCES}
                      INCLUDE_DIRS ${ARDUINO_GFX_DIR}/src
                      REQUIRES arduino-esp32 esp_lcd esp_mm)

# ArduinoGFX uses designated initialisers that trigger "-Werror=narrowing"
# when assigning signed integers to unsigned fields. The upstream sources are
# shared across many projects, so patching the code directly is undesirable.
# Instead, suppress treating these narrowing conversions as errors so the
# library builds cleanly under ESP-IDF 5.5.1.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=narrowing)
