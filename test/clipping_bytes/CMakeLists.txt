find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME clipping_bytes_rust_export
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
    --build ${CMAKE_CURRENT_BINARY_DIR}/rust-export
    --arduinojson ${XP_TEST_ARDUINOJSON_SRC}
    --archive ${XP_FFI_HOST_ARCHIVE}
    --rust-owner --rust-export --negative-controls)
add_test(NAME clipping_bytes_oracle
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
    --build ${CMAKE_CURRENT_BINARY_DIR}/oracle
    --arduinojson ${XP_TEST_ARDUINOJSON_SRC}
    --archive ${XP_FFI_HOST_ARCHIVE}
    --negative-controls)
add_test(NAME clipping_bytes_rust
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
    --build ${CMAKE_CURRENT_BINARY_DIR}/rust-oracle
    --arduinojson ${XP_TEST_ARDUINOJSON_SRC}
    --archive ${XP_FFI_HOST_ARCHIVE}
    --rust-owner --negative-controls)
