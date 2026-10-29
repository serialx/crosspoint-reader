find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME ui_catalog_cpp_goldens COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_oracle.py)
