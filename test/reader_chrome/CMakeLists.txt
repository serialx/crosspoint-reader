find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME reader_chrome_cpp_goldens
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_oracle.py)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/xp_ffi_host.cmake)
add_executable(reader_chrome_abi abi.cpp)
target_link_libraries(reader_chrome_abi PRIVATE xp_ffi_host)
target_compile_options(reader_chrome_abi PRIVATE -UNDEBUG)
add_test(NAME reader_chrome_abi COMMAND reader_chrome_abi)
add_test(NAME reader_chrome_command_allocation
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_commands.py)
add_test(NAME reader_chrome_bookmark_input
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_bookmark_input.py)
