find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME DictionaryEngineOracle COMMAND ${CMAKE_COMMAND} -E env
  XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}
  ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/oracle.py
  --build ${CMAKE_CURRENT_BINARY_DIR}/oracle)
add_test(NAME DictionaryAbiContract COMMAND ${CMAKE_COMMAND} -E env
  XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}
  ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/abi.py
  --build ${CMAKE_CURRENT_BINARY_DIR}/abi --check-mutation)
add_test(NAME DictionaryCompressedLookupOracle COMMAND ${CMAKE_COMMAND} -E env
  XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}
  ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/oracle.py
  --build ${CMAKE_CURRENT_BINARY_DIR}/compressed --compressed)
