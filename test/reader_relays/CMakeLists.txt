add_test(NAME reader_direct_relays
  COMMAND ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${CMAKE_CURRENT_BINARY_DIR}/rust
    cargo test --offline --manifest-path ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml)
