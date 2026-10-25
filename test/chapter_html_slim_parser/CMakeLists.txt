add_executable(ChapterHtmlSlimParserTest
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/Hyphenator.cpp
  ChapterHtmlSlimParserTest.cpp
  ParserLinkStubs.cpp
  DictionaryHtmlPagesTest.cpp
  ${REPO_ROOT}/src/util/DictHtmlPages.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Page.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/CjkBreaks.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp
  ${REPO_ROOT}/lib/Epub/Epub/WordStore.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
)

target_include_directories(ChapterHtmlSlimParserTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/Epub/Epub/parsers
  ${REPO_ROOT}/lib/Epub/Epub/css
  ${REPO_ROOT}/lib/AsciiText
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/XmlParserUtils
)

target_link_libraries(ChapterHtmlSlimParserTest PRIVATE
  crosspoint_test_common
  crosspoint_hyphen_ffi
  GTest::gtest_main
)

gtest_discover_tests(ChapterHtmlSlimParserTest)

include(${REPO_ROOT}/test/cmake/xp_ffi_host.cmake)
target_link_libraries(ChapterHtmlSlimParserTest PRIVATE xp_ffi_host)
target_compile_definitions(ChapterHtmlSlimParserTest PRIVATE XP_FFI_HOST_FAULT_INJECTION)
