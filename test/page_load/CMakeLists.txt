get_target_property(PAGE_LOAD_INCLUDES SectionGoldenDump INCLUDE_DIRECTORIES)
add_executable(PageLoad
  PageLoad.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Section.cpp
  ${REPO_ROOT}/lib/Epub/Epub/SectionCodec.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Page.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/ImageBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Paragraph.cpp
  ${REPO_ROOT}/lib/Epub/Epub/CjkBreaks.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterPageAssembler.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterPageServices.cpp
  ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/Hyphenator.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
  ${REPO_ROOT}/lib/MiniBidi/RustBidi.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/test/section_golden/ImageDecoderStub.cpp)
target_include_directories(PageLoad PRIVATE ${PAGE_LOAD_INCLUDES})
target_link_libraries(PageLoad PRIVATE crosspoint_test_common xp_ffi_host)
add_test(NAME PageLoadMalformed COMMAND PageLoad malformed)
add_test(NAME PageLoadOom COMMAND PageLoad oom)
add_test(NAME PageLoadPartial COMMAND PageLoad partial)
