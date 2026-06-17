# vh_core: flight-critical core. Headers + .cpp implementations grow per
# milestone. Each test is an independent CTest executable.

add_library(vh_core STATIC
  src/manifest.cpp
  src/pgm.cpp
  src/replay_camera.cpp
  src/preprocess.cpp
  src/health.cpp
  src/route_io.cpp
  src/route_inspect.cpp
  src/route_recorder.cpp
  src/direction_error.cpp
  src/route_matcher.cpp
  src/route_quality.cpp
  src/navigator.cpp
  src/mavlink.cpp
  src/telemetry.cpp
  src/command_sink.cpp
  src/dry_run_bridge.cpp
  src/camera_profile.cpp
  src/pi_camera.cpp
  src/match_session.cpp
  src/safety_gate.cpp
  src/live_output.cpp
)
target_include_directories(vh_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# --- Pi hardware capture gate (M11) ------------------------------------------
# VH_ENABLE_LIBCAMERA is PUBLIC: it conditionally declares a pimpl member in
# vh/pi_camera.hpp, so every consumer must see the same value (ABI consistency).
# Desktop builds compile the fail-closed stub; the libcamera dependency is only
# pulled in on Pi builds (-DVH_ENABLE_LIBCAMERA=ON).
if(VH_ENABLE_LIBCAMERA)
  target_compile_definitions(vh_core PUBLIC VH_ENABLE_LIBCAMERA=1)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LIBCAMERA REQUIRED IMPORTED_TARGET libcamera)
  target_link_libraries(vh_core PUBLIC PkgConfig::LIBCAMERA)
  message(STATUS "VH libcamera link:  ${LIBCAMERA_VERSION}")
else()
  target_compile_definitions(vh_core PUBLIC VH_ENABLE_LIBCAMERA=0)
endif()

# --- Tests --------------------------------------------------------------------
function(vh_add_test name)
  add_executable(${name} tests/${name}.cpp)
  target_link_libraries(${name} PRIVATE vh_core)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

vh_add_test(test_sanity)
vh_add_test(test_manifest)
vh_add_test(test_pgm)
vh_add_test(test_replay_camera)
vh_add_test(test_preprocess)
vh_add_test(test_health)
vh_add_test(test_pipeline_harness)
vh_add_test(test_endian_digest)
vh_add_test(test_route_io)
vh_add_test(test_route_inspect)
vh_add_test(test_route_recorder)
vh_add_test(test_route_matcher)
vh_add_test(test_route_quality)
vh_add_test(test_navigator)
vh_add_test(test_mavlink)
vh_add_test(test_telemetry)
vh_add_test(test_command_sink)
vh_add_test(test_dry_run_bridge)
vh_add_test(test_camera_profile)
vh_add_test(test_pi_camera)
vh_add_test(test_match_session)
vh_add_test(test_safety_gate)
vh_add_test(test_live_output)
