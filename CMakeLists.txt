# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include(board_map.cmake)
if(PIO_BOARD_DIR)
  list(APPEND EXTRA_COMPONENT_DIRS ${CMAKE_SOURCE_DIR}/boards/${PIO_BOARD_DIR})
endif()
list(APPEND EXTRA_COMPONENT_DIRS ${CMAKE_SOURCE_DIR}/src)

execute_process(
  COMMAND python3 ${CMAKE_SOURCE_DIR}/parse_pio_flags.py ${PIO_ENV_NAME}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE PIO_FLAG_OUTPUT
  OUTPUT_STRIP_TRAILING_WHITESPACE)
string(REGEX MATCH "FLAGS=([^\n]*)" _ ${PIO_FLAG_OUTPUT})
set(PIO_FLAGS "${CMAKE_MATCH_1}")
if(PIO_FLAGS)
  separate_arguments(PIO_FLAG_LIST NATIVE_COMMAND ${PIO_FLAGS})
  add_compile_options(${PIO_FLAG_LIST})
endif()

set(COMPONENTS src)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Launcher)
