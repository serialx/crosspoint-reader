find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME reader_chrome_cpp_goldens
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_oracle.py)
