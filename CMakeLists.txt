cmake_minimum_required(VERSION 3.21)

project(gb_sprite_codec LANGUAGES C VERSION 0.0.1 DESCRIPTION "Gameboy sprite encoder/decoder")
set(CMAKE_EXPORT_COMPILE_COMMANDS true)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

list(APPEND SOURCE_FILES src/main.c src/sprite.c)
add_compile_options(-g -Wall -Werror -Wextra -Winline -Wpedantic -march=x86-64 -m64 -std=iso9899:2018)
add_executable(GBSprite)

target_include_directories(GBSprite PRIVATE include)
target_sources(GBSprite PRIVATE ${SOURCE_FILES})
set_target_properties(GBSprite PROPERTIES VERSION ${PROJECT_VERSION} PUBLIC_HEADER include/sprite.h)

install(TARGETS GBSprite RUNTIME DESTINATION ${CMAKE_SOURCE_DIR}/build/bin PUBLIC_HEADER DESTINATION ${CMAKE_SOURCE_DIR}/build/include)