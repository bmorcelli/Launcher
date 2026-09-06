# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

file(GLOB_RECURSE app_sources
    ${CMAKE_SOURCE_DIR}/src/*.c
    ${CMAKE_SOURCE_DIR}/src/*.cpp
)
list(FILTER app_sources EXCLUDE REGEX ".*/src/massStorage\\.cpp$")

file(GLOB_RECURSE arduino_gfx_sources
    ${CMAKE_SOURCE_DIR}/lib/Arduino_GFX/src/*.cpp
)
list(APPEND app_sources
    ${CMAKE_SOURCE_DIR}/lib/DisplayDrivers/src/DisplayConfig.cpp
    ${CMAKE_SOURCE_DIR}/lib/DisplayDrivers/src/backends/ardgfx.cpp
    ${arduino_gfx_sources}
)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/lib/DisplayDrivers/src
        ${CMAKE_SOURCE_DIR}/lib/Arduino_GFX/src
)
