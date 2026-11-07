find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME bookmark_bytes_codec
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/codec.py
    --build ${CMAKE_CURRENT_BINARY_DIR}/codec)
add_test(NAME bookmark_bytes_rust
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
    --build ${CMAKE_CURRENT_BINARY_DIR}/rust-oracle
    --arduinojson ${XP_TEST_ARDUINOJSON_SRC}
    --archive ${XP_FFI_HOST_ARCHIVE}
    --rust-owner --negative-controls)
add_test(NAME bookmark_bytes_oracle
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
    --build ${CMAKE_CURRENT_BINARY_DIR}/oracle
    --arduinojson ${XP_TEST_ARDUINOJSON_SRC}
    --archive ${XP_FFI_HOST_ARCHIVE}
    --negative-controls)

add_test(NAME bookmark_progress_oracles
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/progress_oracles.py
    --build ${CMAKE_CURRENT_BINARY_DIR}/progress-oracles)
add_test(NAME bookmark_progress_owner
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/progress_owner.py
    --build ${CMAKE_CURRENT_BINARY_DIR}/progress-owner
    --archive ${XP_FFI_HOST_ARCHIVE})
