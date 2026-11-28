find_program(PAGE_IMAGE_PIN_CARGO cargo REQUIRED)
add_test(NAME PageImagePin COMMAND ${PAGE_IMAGE_PIN_CARGO} test
  --manifest-path ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml
  --target-dir ${CMAKE_CURRENT_BINARY_DIR}/rust --locked --offline)
