cmake_minimum_required(VERSION 3.21)

project(GB_Sprite_Console LANGUAGES C VERSION 0.0.1 DESCRIPTION "Console interface for Gameboy sprite encoder/decoder")
set(CMAKE_EXPORT_COMPILE_COMMANDS true)

add_subdirectory(src)
add_subdirectory(test)

list(APPEND SOURCE_FILES src/main.c)
add_compile_options(-g -Wall -Werror -Wextra -Winline -Wpedantic -march=x86-64 -m64 -std=iso9899:2018)
add_executable(GB_Sprite)
target_include_directories(GB_Sprite PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_sources(GB_Sprite PRIVATE ${SOURCE_FILES})
target_link_libraries(GB_Sprite gbsprite)
set_target_properties(GB_Sprite PROPERTIES PUBLIC_HEADER include/sprite.h)

install(TARGETS GB_Sprite RUNTIME DESTINATION ${CMAKE_SOURCE_DIR}/build/bin PUBLIC_HEADER DESTINATION ${CMAKE_SOURCE_DIR}/build/include)