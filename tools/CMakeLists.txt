# Command-line tools that link against vh_core.

function(vh_add_tool name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE vh_core)
endfunction()

vh_add_tool(vh_route_inspect)
vh_add_tool(vh_route_record)
vh_add_tool(vh_route_quality)
vh_add_tool(vh_mavlink_inspect)
vh_add_tool(vh_camera_profile)
vh_add_tool(vh_match_session)
