add_test(NAME runtime_input_native COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/native.py)
add_test(NAME runtime_input_shipping
  COMMAND ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${CMAKE_CURRENT_BINARY_DIR}/rust
    cargo test --offline --manifest-path ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml)
