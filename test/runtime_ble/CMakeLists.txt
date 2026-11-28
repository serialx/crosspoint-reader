add_test(NAME runtime_direct_ble
  COMMAND ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${CMAKE_CURRENT_BINARY_DIR}/rust
    cargo test --offline --locked --manifest-path ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml)
add_test(NAME runtime_ble_native_phases
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/native.py)
