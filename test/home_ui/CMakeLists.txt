find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME home_ui_cpp_goldens COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_oracle.py)
add_test(NAME home_ui_input_goldens COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/input_oracle.py)
