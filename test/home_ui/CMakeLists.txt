find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME home_ui_cpp_goldens COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_oracle.py)
add_test(NAME home_ui_input_goldens COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/input_oracle.py)
add_test(NAME home_ui_theme_goldens COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/theme_oracle.py)
add_test(NAME home_ui_cache_goldens COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/cache_oracle.py)
add_test(NAME home_ui_rust_differential COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/differential_oracle.py)
add_test(NAME home_ui_grid_goldens COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/grid_oracle.py)
add_test(NAME home_ui_tabs_goldens COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tabs_oracle.py)
add_test(NAME home_ui_touch_goldens COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/touch_oracle.py)

add_test(NAME home_ui_facade_routes COMMAND ${CMAKE_COMMAND} -E env
  "XP_FFI_HOST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
  ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/facade_oracle.py)
