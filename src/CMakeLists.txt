cmake_minimum_required(VERSION 3.1)
project(space-invaders)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../bin)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(../../rizz rizz)

rizz_add_executable(space-invaders main.c)

add_dependencies(space-invaders imgui)