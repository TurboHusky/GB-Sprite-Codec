cmake_minimum_required(VERSION 3.21)

project(gb_sprite_codec_tests LANGUAGES C VERSION 0.0.1 DESCRIPTION "Blackbox unit tests for Gameboy sprite encoder/decoder")
set(CMAKE_EXPORT_COMPILE_COMMANDS true)

set(WITH_EXAMPLES OFF CACHE BOOL "Do not build cmocka examples")
add_subdirectory(cmocka)

list(APPEND SOURCE_FILES sprite_test.c)
add_executable(gb_sprite_tests ${SOURCE_FILES})
target_compile_options(gb_sprite_tests PRIVATE ${PROJECT_COMPILER_FLAGS})
target_include_directories(gb_sprite_tests PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/cmocka/include)
target_link_libraries(gb_sprite_tests cmocka gbsprite)
set_target_properties(gb_sprite_tests PROPERTIES VERSION ${PROJECT_VERSION})