add_executable(KoreanLineBreakingTest
  KoreanLineBreakingTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Paragraph.cpp
  ${REPO_ROOT}/lib/Epub/Epub/SectionCodec.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/CjkBreaks.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/Hyphenator.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
  ${REPO_ROOT}/lib/MiniBidi/RustBidi.cpp

  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(KoreanLineBreakingTest PRIVATE
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/FsHelpers
)

target_link_libraries(KoreanLineBreakingTest PRIVATE
  xp_ffi_host
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(KoreanLineBreakingTest)

target_sources(KoreanLineBreakingTest PRIVATE ${REPO_ROOT}/test/page_codec_diff/TextSerialization.cpp)
target_compile_definitions(KoreanLineBreakingTest PRIVATE XP_PAGE_CODEC_TEST=1)
