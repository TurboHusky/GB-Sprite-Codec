cmake_minimum_required(VERSION 3.21)

project(GB_Sprite_Console LANGUAGES C VERSION 0.0.1 DESCRIPTION "Console interface for Gameboy sprite encoder/decoder")
set(CMAKE_EXPORT_COMPILE_COMMANDS true)

if (CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(PROJECT_COMPILER_FLAGS ${CMAKE_C_FLAGS} -g -Wall -Werror -Wextra -Winline -Wpedantic -march=x86-64 -m64 -std=iso9899:2018)
elseif (CMAKE_C_COMPILER_ID STREQUAL "MSVC")
  set(PROJECT_COMPILER_FLAGS ${CMAKE_C_FLAGS} /Wall /std:c17)
endif()

add_subdirectory(src)
add_subdirectory(test)

list(APPEND SOURCE_FILES src/main.c)
add_executable(GB_Sprite)
target_compile_options(GB_Sprite PRIVATE ${PROJECT_COMPILER_FLAGS})
target_include_directories(GB_Sprite PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_sources(GB_Sprite PRIVATE ${SOURCE_FILES})
target_link_libraries(GB_Sprite gbsprite)
set_target_properties(GB_Sprite PROPERTIES PUBLIC_HEADER include/sprite.h)

install(TARGETS GB_Sprite RUNTIME DESTINATION ${CMAKE_SOURCE_DIR}/build/bin PUBLIC_HEADER DESTINATION ${CMAKE_SOURCE_DIR}/build/include)