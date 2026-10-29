find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME boot_ui_cpp_oracle COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_oracle.py)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/xp_ffi_host.cmake)
add_executable(boot_ui_abi abi.cpp)
target_link_libraries(boot_ui_abi PRIVATE xp_ffi_host)
target_compile_options(boot_ui_abi PRIVATE -UNDEBUG)
add_test(NAME boot_ui_abi COMMAND boot_ui_abi)
