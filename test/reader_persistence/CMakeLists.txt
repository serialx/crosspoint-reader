find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME reader_persistence_order
  COMMAND ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${CMAKE_CURRENT_BINARY_DIR}/rust
    cargo test --offline --manifest-path ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml)
add_test(NAME reader_store_parity
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run_stores.py
    --build ${CMAKE_CURRENT_BINARY_DIR}/stores --arduinojson ${XP_TEST_ARDUINOJSON_SRC})
