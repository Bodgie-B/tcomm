cmake_minimum_required(VERSION 3.16)
project(tcomm_screensaver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# core/: portable, no exceptions, no RTTI, strict warnings. These flags are the
# same ones the ESP32 build will use, so anything that compiles here with no
# warnings is expected to compile there (unverified until stage 2).
file(GLOB_RECURSE SS_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp)
add_library(ss_core STATIC ${SS_CORE_SOURCES})
target_include_directories(ss_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
# The Christmas season (core/scene/xmas.h; Mike, 2026-11-26): on, as the code's own default is for the panel
# firmware (which compiles core/ with no defines). -DSS_XMAS=OFF builds the core without any of it: outside the
# season (Nov 26 to Jan 1) every plan and frame must be the same either way.
option(SS_XMAS "Build the Christmas season" ON)
if(SS_XMAS)
  target_compile_definitions(ss_core PUBLIC SS_XMAS=1)
else()
  target_compile_definitions(ss_core PUBLIC SS_XMAS=0)
endif()
# Stage 22's plan switch (core/scene/stage22_switch.h; Mike, 2026-11-18): off keeps every plan as it was
# (the proof that nothing else moved), on lets the new pieces into the plans. It changes no struct, so
# both settings have the same World. ON by default since the stage went live (step C7, 2026-11-19), as the
# header's own default is for the panel; build-s22-off passes -DSS_S22=OFF.
option(SS_S22 "Let stage 22's new pieces into the plans" ON)
if(SS_S22)
  target_compile_definitions(ss_core PUBLIC SS_S22=1)
else()
  target_compile_definitions(ss_core PUBLIC SS_S22=0)
endif()
target_compile_options(ss_core PRIVATE
  -fno-exceptions -fno-rtti
  -Wall -Wextra -Wconversion -Wdouble-promotion -Wshadow -Werror
  # No FMA contraction, so PC and ESP32 float results match (ESP-IDF's gnu++17 default allows it).
  -ffp-contract=off)

# Tests: plain executables, no framework. Each returns non-zero on failure.
enable_testing()
file(GLOB SS_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp)
foreach(src ${SS_TEST_SOURCES})
  get_filename_component(name ${src} NAME_WE)
  add_executable(${name} ${src})
  target_link_libraries(${name} ss_core)
  target_compile_options(${name} PRIVATE -Wall -Wextra)
  add_test(NAME ${name} COMMAND ${name})
endforeach()

# Headless preview: renders frames to PPM files. Always built.
add_executable(ss_preview sim/preview_main.cpp sim/preview_common.cpp)
target_link_libraries(ss_preview ss_core)
target_compile_options(ss_preview PRIVATE -Wall -Wextra)

# Soak test: simulated clock and inputs, every invariant checked every tick (sim/soak_main.cpp).
find_package(Threads REQUIRED)   # the soak's hang watchdog is a std::thread
add_executable(ss_soak sim/soak_main.cpp)
target_link_libraries(ss_soak ss_core Threads::Threads)
target_compile_options(ss_soak PRIVATE -Wall -Wextra)

# Windowed preview: built only when SDL2 is found. Not verified in the cloud
# build container (no SDL2 there); intended for Mike's laptop.
find_package(SDL2 QUIET)
if(SDL2_FOUND)
  add_executable(ss_window sim/window_main.cpp sim/preview_common.cpp)
  target_link_libraries(ss_window ss_core)
  if(TARGET SDL2::SDL2)
    target_link_libraries(ss_window SDL2::SDL2)
  else()
    target_include_directories(ss_window PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(ss_window ${SDL2_LIBRARIES})
  endif()
  if(TARGET SDL2::SDL2main)
    target_link_libraries(ss_window SDL2::SDL2main)
  endif()
  target_compile_options(ss_window PRIVATE -Wall -Wextra)
  message(STATUS "SDL2 found: building ss_window")
else()
  message(STATUS "SDL2 not found: skipping ss_window (headless ss_preview still builds)")
endif()
