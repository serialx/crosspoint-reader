find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ParsedTextTrace.cpp
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/instrument_layout.py
    ${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp ${CMAKE_CURRENT_BINARY_DIR}/ParsedTextTrace.cpp
  DEPENDS ${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp instrument_layout.py
  VERBATIM)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ChapterTrace.cpp
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/instrument_chapter.py
    ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp ${CMAKE_CURRENT_BINARY_DIR}/ChapterTrace.cpp
    ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterPageAssembler.cpp
    ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterSemantics.cpp
  DEPENDS ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp
    ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterPageAssembler.cpp
    ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterSemantics.cpp instrument_chapter.py
  VERBATIM)
add_executable(SectionGoldenDump
  SectionGoldenDump.cpp
  ImageDecoderStub.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Section.cpp
  ${REPO_ROOT}/lib/Epub/Epub/SectionCodec.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Page.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/ImageBlock.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/ParsedTextTrace.cpp
  ${REPO_ROOT}/lib/Epub/Epub/WordStore.cpp
  ${REPO_ROOT}/lib/Epub/Epub/CjkBreaks.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/ChapterTrace.cpp
  ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/Hyphenator.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
)
target_include_directories(SectionGoldenDump PRIVATE
  ${REPO_ROOT}/lib/Epub/Epub/parsers
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/test/fs_helpers/stubs
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/Epub/Epub/css
  ${REPO_ROOT}/lib/AsciiText
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/XmlParserUtils
)
target_link_libraries(SectionGoldenDump PRIVATE crosspoint_test_common xp_ffi_host)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME SectionGoldens COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/run.py --dump $<TARGET_FILE:SectionGoldenDump>)
add_test(NAME SectionGoldenComparator COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/test_oracle.py)
add_test(NAME SectionReadGoldens COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/read_oracle.py --dump $<TARGET_FILE:SectionGoldenDump>
  --output ${CMAKE_CURRENT_SOURCE_DIR}/goldens/reads.json.gz --verify)

add_test(NAME LayoutGoldens COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/run.py --dump $<TARGET_FILE:SectionGoldenDump> --layout)

add_test(NAME ChapterOperationGoldens COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/run.py --dump $<TARGET_FILE:SectionGoldenDump> --ops)
add_test(NAME ChapterOperationInstrumentation COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/test_chapter_trace.py)
add_test(NAME SectionPartialReadGoldens COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/read_oracle.py --dump $<TARGET_FILE:SectionGoldenDump>
  --output ${CMAKE_CURRENT_SOURCE_DIR}/goldens/partials.json.gz --partial --verify)
