cmake_minimum_required(VERSION 3.16)
project(blockout C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Embed the font so the binary has no runtime asset paths.
set(FONT_SRC ${CMAKE_CURRENT_SOURCE_DIR}/assets/Exo2.ttf)
set(FONT_C ${CMAKE_CURRENT_BINARY_DIR}/font_data.c)
add_custom_command(
  OUTPUT ${FONT_C}
  COMMAND ${CMAKE_COMMAND} -DIN=${FONT_SRC} -DOUT=${FONT_C} -DNAME=font_ttf -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
  DEPENDS ${FONT_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake)

add_library(bocore STATIC core/bo_core.c core/bo_tables.c)
target_include_directories(bocore PUBLIC core)

add_executable(blockout src/main.c src/gfx.c src/font.c src/view.c src/ui.c src/audio.c src/store.c src/hof.c ${FONT_C})
target_link_libraries(blockout PRIVATE bocore)

if(EMSCRIPTEN)
  set(SDL_FLAGS "-sUSE_SDL=2")
  target_compile_options(blockout PRIVATE ${SDL_FLAGS})
  target_link_options(blockout PRIVATE ${SDL_FLAGS} -sALLOW_MEMORY_GROWTH=1 -lidbfs.js
    --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html)
  set_target_properties(blockout PROPERTIES SUFFIX ".html")
else()
  find_package(SDL2 REQUIRED)
  target_link_libraries(blockout PRIVATE SDL2::SDL2 m)
  if(TARGET SDL2::SDL2main)
    target_link_libraries(blockout PRIVATE SDL2::SDL2main)
  endif()
endif()

if(NOT MSVC)
  target_compile_options(bocore PRIVATE -Wall -Wextra)
  target_compile_options(blockout PRIVATE -Wall -Wextra)
endif()

# Replay tool used by re/emu/difftest.py
add_executable(bo_replay tests/replay.c)
target_link_libraries(bo_replay PRIVATE bocore)
