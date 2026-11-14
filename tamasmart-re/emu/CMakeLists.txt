cmake_minimum_required(VERSION 3.16)
project(tsmart CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(tsmart_core STATIC core/cpu.cpp)
target_include_directories(tsmart_core PUBLIC core)

# Headless runner: no dependencies.
add_executable(tsmart-run headless/main.cpp)
target_link_libraries(tsmart-run PRIVATE tsmart_core)

# Desktop app: needs SDL2 (MSYS2: pacman -S mingw-w64-ucrt-x86_64-SDL2; vcpkg: vcpkg install sdl2).
find_package(SDL2 CONFIG QUIET)
if(SDL2_FOUND)
  add_executable(tsmart sdl/main.cpp)
  if(TARGET SDL2::SDL2)
    target_link_libraries(tsmart PRIVATE tsmart_core SDL2::SDL2)
  else()
    target_link_libraries(tsmart PRIVATE tsmart_core SDL2::SDL2-static)
  endif()
else()
  message(STATUS "SDL2 not found: building only the headless runner")
endif()
