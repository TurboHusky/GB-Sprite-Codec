cmake_minimum_required(VERSION 3.21)

project(gb_sprite_codec LANGUAGES C VERSION 0.0.1 DESCRIPTION "Gameboy sprite encoder/decoder")
set(CMAKE_EXPORT_COMPILE_COMMANDS true)

list(APPEND LIB_SOURCE_FILES sprite.c)

add_library(gbsprite STATIC)
target_compile_options(gbsprite PRIVATE ${PROJECT_COMPILER_FLAGS})
target_include_directories(gbsprite PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_sources(gbsprite PRIVATE ${LIB_SOURCE_FILES})
set_target_properties(gbsprite PROPERTIES VERSION ${PROJECT_VERSION} PUBLIC_HEADER ${CMAKE_SOURCE_DIR}/include/sprite.h)