find_program(CARGO_EXECUTABLE cargo REQUIRED)
find_package(ZLIB REQUIRED)
set(HYPHEN_RUST_TARGET "${CMAKE_BINARY_DIR}/hyphen-rust")
file(GLOB_RECURSE HYPHEN_RUST_SOURCES CONFIGURE_DEPENDS
  "${REPO_ROOT}/rust/crates/xp-core/src/*.rs"
  "${REPO_ROOT}/rust/crates/xp-core/data/hyph/*.bin"
  "${REPO_ROOT}/rust/crates/xp-ffi/src/*.rs"
)
add_custom_command(OUTPUT "${HYPHEN_RUST_TARGET}/release/libxp_ffi.a"
  COMMAND ${CARGO_EXECUTABLE} build --manifest-path "${REPO_ROOT}/rust/Cargo.toml"
    --target-dir "${HYPHEN_RUST_TARGET}" -p xp-ffi --release --locked
  DEPENDS ${HYPHEN_RUST_SOURCES} "${REPO_ROOT}/rust/Cargo.toml"
    "${REPO_ROOT}/rust/Cargo.lock"
    "${REPO_ROOT}/rust/crates/xp-core/Cargo.toml"
    "${REPO_ROOT}/rust/crates/xp-ffi/Cargo.toml"
  VERBATIM
)
add_custom_target(hyphen_rust_archive DEPENDS "${HYPHEN_RUST_TARGET}/release/libxp_ffi.a")
add_library(crosspoint_hyphen_ffi STATIC IMPORTED GLOBAL)
set_target_properties(crosspoint_hyphen_ffi PROPERTIES
  IMPORTED_LOCATION "${HYPHEN_RUST_TARGET}/release/libxp_ffi.a"
  INTERFACE_INCLUDE_DIRECTORIES "${REPO_ROOT}/rust/crates/xp-ffi/include"
)
add_dependencies(crosspoint_hyphen_ffi hyphen_rust_archive)

add_executable(HyphenationEvaluationTest
  HyphenationEvaluationTest.cpp
  GoldenTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/Hyphenator.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(HyphenationEvaluationTest PRIVATE
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/lib/Utf8
)

target_compile_definitions(HyphenationEvaluationTest PRIVATE
  HYPHENATION_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  HYPHENATION_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
)

target_link_libraries(HyphenationEvaluationTest PRIVATE
  crosspoint_hyphen_ffi
  crosspoint_test_common
  GTest::gtest_main
  ZLIB::ZLIB
)

gtest_discover_tests(HyphenationEvaluationTest)
