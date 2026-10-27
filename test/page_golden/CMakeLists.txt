cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(page_golden C CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  get_filename_component(REPO_ROOT "../.." ABSOLUTE)
  include(../cmake/xp_ffi_host.cmake)
  enable_testing()
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(FONT_SDK ${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont)
file(GLOB FT_SOURCES ${FONT_SDK}/src/freetype/*.c)
set(INSTRUMENTED ${CMAKE_CURRENT_BINARY_DIR}/instrumented)
add_custom_command(OUTPUT ${INSTRUMENTED}/GfxRenderer.cpp ${INSTRUMENTED}/GfxRenderer.h ${INSTRUMENTED}/Page.cpp ${INSTRUMENTED}/TextBlock.cpp
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/instrument.py ${REPO_ROOT} ${INSTRUMENTED}
  DEPENDS ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp instrument.py ${REPO_ROOT}/lib/Epub/Epub/Page.cpp ${REPO_ROOT}/test/text_golden/instrument.py ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.h
  VERBATIM)
add_executable(PageGolden PageGolden.cpp ${INSTRUMENTED}/GfxRenderer.cpp
  ${REPO_ROOT}/lib/GfxRenderer/FontCacheManager.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFont.cpp ${REPO_ROOT}/lib/EpdFont/FontDecompressor.cpp
  ${REPO_ROOT}/lib/EpdFont/TtfEpdFont.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Section.cpp
  ${REPO_ROOT}/lib/Epub/Epub/SectionCodec.cpp
  ${INSTRUMENTED}/Page.cpp
  ${INSTRUMENTED}/TextBlock.cpp
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
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ImageDecoderStub.cpp
  ${REPO_ROOT}/lib/MiniBidi/RustBidi.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp
  ${REPO_ROOT}/lib/uzlib/src/tinflate.c ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${FONT_SDK}/src/FtFont.cpp ${FONT_SDK}/src/FontAlloc.c ${FONT_SDK}/src/Gsub.cpp ${FONT_SDK}/src/Gpos.cpp
  ${FT_SOURCES})
target_include_directories(PageGolden PRIVATE ${INSTRUMENTED} stubs .
  ${REPO_ROOT}/test/text_golden/stubs ${REPO_ROOT}/test/fs_helpers/stubs ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/lib ${REPO_ROOT}/lib/FsHelpers ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub ${REPO_ROOT}/lib/Epub/Epub/parsers
  ${REPO_ROOT}/lib/Epub/Epub/css ${REPO_ROOT}/lib/Epub/Epub/blocks ${REPO_ROOT}/lib/AsciiText
  ${REPO_ROOT}/lib/Serialization ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/src ${REPO_ROOT}/lib/GfxRenderer ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/MiniBidi ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/InflateReader ${REPO_ROOT}/lib/uzlib/src
  ${FONT_SDK}/include ${FONT_SDK}/third_party/freetype/include ${FONT_SDK}/third_party/stb)
target_compile_definitions(PageGolden PRIVATE BOARD_HAS_PSRAM=1
  FREEINK_FONT_ENABLE_AUTOHINT=1)
target_compile_options(PageGolden PRIVATE -ffunction-sections -fdata-sections -fno-exceptions)
if(APPLE)
  target_link_options(PageGolden PRIVATE -Wl,-dead_strip)
else()
  target_link_options(PageGolden PRIVATE -Wl,--gc-sections)
endif()
target_link_libraries(PageGolden PRIVATE xp_ffi_host)
add_test(NAME PageGolden COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py check
  --binary $<TARGET_FILE:PageGolden> --work-dir ${CMAKE_CURRENT_BINARY_DIR}/check)
add_test(NAME PageGoldenComparator COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_oracle.py)

target_sources(PageGolden PRIVATE ${REPO_ROOT}/test/page_codec_diff/Serialization.cpp)
target_compile_definitions(PageGolden PRIVATE XP_PAGE_CODEC_TEST=1)

target_sources(PageGolden PRIVATE ${REPO_ROOT}/test/page_codec_diff/TextSerialization.cpp)
