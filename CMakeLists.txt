cmake_minimum_required(VERSION 3.22)
project(visual_homing_codex
  VERSION 0.1.0
  DESCRIPTION "Flight-safety-oriented visual route-following command-assist core"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Safety-critical compile-time scopes -------------------------------------
# All live-output options default OFF. Ordinary scripts must also pass them
# explicitly as OFF so a stale CMake cache can never silently enable output.
option(VH_ENABLE_LIVE_OUTPUT "Enable live MAVLink output scope (reviewed bench only)" OFF)
option(VH_ENABLE_BENCH_PROPS_OFF "Enable bench props-off scope (reviewed bench only)" OFF)
option(VH_ENABLE_WRITER_ATTACH "Enable serial writer attachment (separate reviewed step)" OFF)

# Hardware capture is opt-in (Pi builds only)
option(VH_ENABLE_LIBCAMERA "Enable Pi libcamera capture backend" OFF)
option(VH_ENABLE_SERIAL_TELEMETRY "Enable live serial MAVLink telemetry" OFF)

# Live output requires the full explicit chain — refuse partial configurations.
if(VH_ENABLE_WRITER_ATTACH AND NOT (VH_ENABLE_LIVE_OUTPUT AND VH_ENABLE_BENCH_PROPS_OFF))
  message(FATAL_ERROR
    "VH_ENABLE_WRITER_ATTACH requires VH_ENABLE_LIVE_OUTPUT=ON and "
    "VH_ENABLE_BENCH_PROPS_OFF=ON. Partial live-output configuration is refused.")
endif()
if(VH_ENABLE_LIVE_OUTPUT AND NOT VH_ENABLE_BENCH_PROPS_OFF)
  message(FATAL_ERROR
    "VH_ENABLE_LIVE_OUTPUT requires VH_ENABLE_BENCH_PROPS_OFF=ON. "
    "The first live-output boundary is bench-only, propellers removed.")
endif()

message(STATUS "VH live output:     ${VH_ENABLE_LIVE_OUTPUT}")
message(STATUS "VH bench props-off: ${VH_ENABLE_BENCH_PROPS_OFF}")
message(STATUS "VH writer attach:   ${VH_ENABLE_WRITER_ATTACH}")
message(STATUS "VH libcamera:       ${VH_ENABLE_LIBCAMERA}")
message(STATUS "VH serial telem:    ${VH_ENABLE_SERIAL_TELEMETRY}")

# --- Warnings as errors on the flight-critical core ---------------------------
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

enable_testing()
add_subdirectory(core)
