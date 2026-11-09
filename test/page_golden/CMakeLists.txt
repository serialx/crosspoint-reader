cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(page_golden C CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  get_filename_component(REPO_ROOT "../.." ABSOLUTE)
  if(STRIP_SPIKE OR STRIP_PLAN)
    set(XP_FFI_HOST_FEATURES "chapter-test,chapter-trace,package-trace,app-test,section-test,reader-strips,display-panel" CACHE STRING "Cargo features for host tests")
  endif()
  include(../cmake/xp_ffi_host.cmake)
  enable_testing()
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(FONT_SDK ${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont)
file(GLOB FT_SOURCES ${FONT_SDK}/src/freetype/*.c)
set(INSTRUMENTED ${CMAKE_CURRENT_BINARY_DIR}/instrumented)
add_custom_command(OUTPUT ${INSTRUMENTED}/GfxRenderer.cpp ${INSTRUMENTED}/GfxRenderer.h ${INSTRUMENTED}/Page.cpp ${INSTRUMENTED}/TextBlock.cpp ${INSTRUMENTED}/ImageBlock.cpp
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/instrument.py ${REPO_ROOT} ${INSTRUMENTED}
  DEPENDS ${REPO_ROOT}/lib/Epub/Epub/blocks/ImageBlock.cpp ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp instrument.py ${REPO_ROOT}/lib/Epub/Epub/Page.cpp ${REPO_ROOT}/test/text_golden/instrument.py ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.h
  VERBATIM)
option(STRIP_IMAGES "Exercise decoded image cache playback in the strip matrix" OFF)
if(STRIP_IMAGES)
  set(PAGE_IMAGE_SOURCE ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDecoderFactory.cpp)
else()
  set(PAGE_IMAGE_SOURCE ImageDecoderStub.cpp)
endif()
add_executable(PageGolden PageGolden.cpp ${INSTRUMENTED}/GfxRenderer.cpp
  ${REPO_ROOT}/lib/expat/ExpatErrors.cpp
  ${REPO_ROOT}/lib/GfxRenderer/FontCacheManager.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFont.cpp ${REPO_ROOT}/lib/EpdFont/FontDecompressor.cpp
  ${REPO_ROOT}/lib/EpdFont/TtfEpdFont.cpp
  ${REPO_ROOT}/lib/Epub/Epub/SectionPlatform.cpp
  ${REPO_ROOT}/test/section_io/SectionTestCodec.cpp
  ${INSTRUMENTED}/Page.cpp
  ${INSTRUMENTED}/TextBlock.cpp
  ${INSTRUMENTED}/ImageBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Paragraph.cpp
  ${REPO_ROOT}/lib/Epub/Epub/CjkBreaks.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterPlatform.cpp
  ${REPO_ROOT}/lib/Epub/Epub/css/CssPlatform.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/Hyphenator.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${PAGE_IMAGE_SOURCE}
  ${REPO_ROOT}/lib/MiniBidi/RustBidi.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
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
  ${REPO_ROOT}/lib/InflateReader
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
option(STRIP_SPIKE "Host strip proof; reader-strips is automatic unless XP_FFI_HOST_FEATURES is overridden" OFF)
set(PAGE_GOLDEN_MODE)
set(PAGE_GOLDEN_WORK check)
if(STRIP_SPIKE)
  list(APPEND PAGE_GOLDEN_MODE --strips)
  set(PAGE_GOLDEN_WORK check-strips)
  if(STRIP_IMAGES)
    list(APPEND PAGE_GOLDEN_MODE --decoded-images)
    set(PAGE_GOLDEN_WORK check-strips-decoded)
  endif()
endif()
add_test(NAME PageGolden COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py check
  --binary $<TARGET_FILE:PageGolden> ${PAGE_GOLDEN_MODE} --work-dir ${CMAKE_CURRENT_BINARY_DIR}/${PAGE_GOLDEN_WORK})
add_test(NAME PageGoldenComparator COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_oracle.py)

target_sources(PageGolden PRIVATE ${REPO_ROOT}/test/page_codec_diff/Serialization.cpp)
target_compile_definitions(PageGolden PRIVATE XP_PAGE_CODEC_TEST=1 XP_FFI_HOST_FAULT_INJECTION=1)

target_sources(PageGolden PRIVATE ${REPO_ROOT}/test/page_codec_diff/TextSerialization.cpp)

if(STRIP_SPIKE)
  target_compile_definitions(PageGolden PRIVATE XP_STRIP_SPIKE=1)
endif()

if(STRIP_IMAGES)
  set(DECODER_SOURCE_DIR "" CACHE PATH "Patched PlatformIO libdeps")
  target_sources(PageGolden PRIVATE
    ${REPO_ROOT}/lib/Epub/Epub/converters/PngToFramebufferConverter.cpp
    ${REPO_ROOT}/lib/Epub/Epub/converters/GifToFramebufferConverter.cpp
    ${REPO_ROOT}/lib/Epub/Epub/converters/JpegToFramebufferConverter.cpp
    ${DECODER_SOURCE_DIR}/JPEGDEC/src/JPEGDEC.cpp)
  target_include_directories(PageGolden PRIVATE ${REPO_ROOT}/lib/GifSupport ${REPO_ROOT}/lib/PngDiagnostics
    ${DECODER_SOURCE_DIR}/JPEGDEC/src)
  target_compile_definitions(PageGolden PRIVATE __LINUX__ XP_STRIP_IMAGES=1)
endif()

option(STRIP_PLAN "Replay retained word placements in strip matrices" OFF)
if(STRIP_PLAN)
  target_compile_definitions(PageGolden PRIVATE XP_STRIP_PLAN=1)
endif()

# Use the frozen parent painter and real controllers without tracing replacements.
if(STRIP_PLAN AND XP_FFI_HOST_FEATURES MATCHES "display-panel")
  get_target_property(REPLAY_SOURCES PageGolden SOURCES)
  list(REMOVE_ITEM REPLAY_SOURCES PageGolden.cpp ${INSTRUMENTED}/GfxRenderer.cpp
    ${INSTRUMENTED}/Page.cpp ${INSTRUMENTED}/TextBlock.cpp ${INSTRUMENTED}/ImageBlock.cpp)
  add_executable(ReaderStripReplay ReaderStripReplay.cpp ${REPLAY_SOURCES}
    ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp ${REPO_ROOT}/lib/Epub/Epub/Page.cpp
    ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp ${REPO_ROOT}/lib/Epub/Epub/blocks/ImageBlock.cpp)
  get_target_property(REPLAY_INCLUDES PageGolden INCLUDE_DIRECTORIES)
  list(REMOVE_ITEM REPLAY_INCLUDES ${INSTRUMENTED})
  target_include_directories(ReaderStripReplay PRIVATE ${REPO_ROOT}/lib/GfxRenderer ${REPLAY_INCLUDES})
  target_include_directories(ReaderStripReplay PRIVATE ${REPO_ROOT}/test/reader_lifecycle/native_render)
  get_target_property(REPLAY_DEFINITIONS PageGolden COMPILE_DEFINITIONS)
  list(REMOVE_ITEM REPLAY_DEFINITIONS XP_STRIP_SPIKE=1 XP_STRIP_PLAN=1)
  target_compile_definitions(ReaderStripReplay PRIVATE ${REPLAY_DEFINITIONS} XP_STRIP_RENDER=1)
  target_compile_options(ReaderStripReplay PRIVATE -ffunction-sections -fdata-sections -fno-exceptions)
  if(APPLE)
    target_link_options(ReaderStripReplay PRIVATE -Wl,-dead_strip)
  else()
    target_link_options(ReaderStripReplay PRIVATE -Wl,--gc-sections)
  endif()
  target_link_libraries(ReaderStripReplay PRIVATE xp_ffi_host)
  add_test(NAME ReaderStripReplay COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/strip_replay.py
    $<TARGET_FILE:ReaderStripReplay>)
  get_target_property(STATUS_SOURCES ReaderStripReplay SOURCES)
  list(REMOVE_ITEM STATUS_SOURCES ReaderStripReplay.cpp)
  add_executable(StatusStripBoundary StatusStripBoundary.cpp ${STATUS_SOURCES})
  target_include_directories(StatusStripBoundary PRIVATE ${REPO_ROOT}/lib/GfxRenderer ${REPLAY_INCLUDES})
  target_compile_definitions(StatusStripBoundary PRIVATE ${REPLAY_DEFINITIONS} XP_STRIP_RENDER=1)
  target_compile_options(StatusStripBoundary PRIVATE -UNDEBUG -ffunction-sections -fdata-sections -fno-exceptions)
  target_link_options(StatusStripBoundary PRIVATE $<TARGET_PROPERTY:ReaderStripReplay,LINK_OPTIONS>)
  target_link_libraries(StatusStripBoundary PRIVATE xp_ffi_host)
  add_test(NAME StatusStripBoundary COMMAND StatusStripBoundary)
endif()

include(${REPO_ROOT}/test/reader_section/fixture.cmake)
reader_section_fixture(PageGolden)

get_target_property(CONTRACT_SOURCES PageGolden SOURCES)
list(REMOVE_ITEM CONTRACT_SOURCES PageGolden.cpp ${INSTRUMENTED}/GfxRenderer.cpp
  ${INSTRUMENTED}/Page.cpp ${INSTRUMENTED}/TextBlock.cpp ${INSTRUMENTED}/ImageBlock.cpp)
set(CONTRACT_RENDERER ${CMAKE_CURRENT_BINARY_DIR}/ContractsRenderer.cpp)
add_custom_command(OUTPUT ${CONTRACT_RENDERER}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/contracts_instrument.py ${REPO_ROOT} ${CONTRACT_RENDERER}
  DEPENDS contracts_instrument.py ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp VERBATIM)
add_executable(PageDirectContracts PageDirectContracts.cpp ${CONTRACT_SOURCES}
  ${CONTRACT_RENDERER} ${REPO_ROOT}/lib/Epub/Epub/Page.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp ${REPO_ROOT}/lib/Epub/Epub/blocks/ImageBlock.cpp)
get_target_property(CONTRACT_INCLUDES PageGolden INCLUDE_DIRECTORIES)
list(REMOVE_ITEM CONTRACT_INCLUDES ${INSTRUMENTED})
target_include_directories(PageDirectContracts PRIVATE ${REPO_ROOT}/lib/GfxRenderer ${CONTRACT_INCLUDES})
get_target_property(CONTRACT_DEFINITIONS PageGolden COMPILE_DEFINITIONS)
list(REMOVE_ITEM CONTRACT_DEFINITIONS XP_STRIP_SPIKE=1 XP_STRIP_PLAN=1)
target_compile_definitions(PageDirectContracts PRIVATE ${CONTRACT_DEFINITIONS})
target_compile_options(PageDirectContracts PRIVATE -ffunction-sections -fdata-sections -fno-exceptions)
target_link_options(PageDirectContracts PRIVATE $<TARGET_PROPERTY:PageGolden,LINK_OPTIONS>)
target_link_libraries(PageDirectContracts PRIVATE xp_ffi_host)
add_test(NAME PageDirectContracts COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/direct_contracts.py $<TARGET_FILE:PageDirectContracts>)

add_test(NAME PageGoldenHistoricalImagePaths COMMAND ${Python3_EXECUTABLE}
  ${REPO_ROOT}/test/reader_section/page_paths.py --binary $<TARGET_FILE:PageGolden>
  --work-dir ${CMAKE_CURRENT_BINARY_DIR}/historical-image-paths)
