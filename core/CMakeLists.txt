# vh_core: header-only for now; becomes a static library when first .cpp lands.
add_library(vh_core INTERFACE)
target_include_directories(vh_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# --- Tests --------------------------------------------------------------------
# Each test file is an independent executable registered with CTest.
function(vh_add_test name)
  add_executable(${name} tests/${name}.cpp)
  target_link_libraries(${name} PRIVATE vh_core)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

vh_add_test(test_sanity)
