cmake_minimum_required(VERSION 3.16)
project(blockout C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

add_executable(blockout src/main.c src/gfx.c src/font.c src/view.c src/ui.c src/audio.c src/store.c src/hof.c third_party/stb_truetype_impl.c ${FONT_C})
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

# Replay and bot tools used by re/emu/difftest.py
add_executable(bo_replay tests/replay.c)
target_link_libraries(bo_replay PRIVATE bocore)
add_executable(bo_botgen tests/botgen.c)
target_link_libraries(bo_botgen PRIVATE bocore)

option(BO_WERROR "Treat warnings as errors" OFF)
if(NOT MSVC)
  set(BO_WARN -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wcast-qual -Wstrict-prototypes
      -Wmissing-prototypes -Wnull-dereference -Wvla -Wdouble-promotion)
  if(BO_WERROR)
    list(APPEND BO_WARN -Werror)
  endif()
  # The core reproduces 16-bit arithmetic, so every narrowing must be explicit there.
  set(BO_WARN_STRICT ${BO_WARN} -Wconversion -Wsign-conversion)
  target_compile_options(bocore PRIVATE ${BO_WARN_STRICT})
  target_compile_options(bo_replay PRIVATE ${BO_WARN_STRICT})
  target_compile_options(bo_botgen PRIVATE ${BO_WARN_STRICT})
  target_compile_options(blockout PRIVATE ${BO_WARN})
  # stb_truetype is third-party
  set_source_files_properties(third_party/stb_truetype_impl.c PROPERTIES COMPILE_OPTIONS "-w")
endif()

