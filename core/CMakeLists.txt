# vh_core: flight-critical core. Headers + .cpp implementations grow per
# milestone. Each test is an independent CTest executable.

add_library(vh_core STATIC
  src/manifest.cpp
  src/pgm.cpp
  src/replay_camera.cpp
  src/preprocess.cpp
  src/health.cpp
)
target_include_directories(vh_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

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
