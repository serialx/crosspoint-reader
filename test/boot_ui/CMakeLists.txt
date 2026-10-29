find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME boot_ui_cpp_oracle COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_oracle.py)
