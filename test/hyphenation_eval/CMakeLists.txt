find_package(ZLIB REQUIRED)

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
  xp_ffi_host
  crosspoint_test_common
  GTest::gtest_main
  ZLIB::ZLIB
)

gtest_discover_tests(HyphenationEvaluationTest)
