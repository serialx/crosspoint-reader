find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_executable(PageCodecDiff
  PageCodecDiff.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Page.cpp
  ${REPO_ROOT}/lib/Epub/Epub/SectionCodec.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/ImageBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/CjkBreaks.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${REPO_ROOT}/test/section_golden/ImageDecoderStub.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/MiniBidi/RustBidi.cpp)
get_target_property(PAGE_CODEC_INCLUDES SectionGoldenDump INCLUDE_DIRECTORIES)
target_include_directories(PageCodecDiff PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs ${PAGE_CODEC_INCLUDES})
target_link_libraries(PageCodecDiff PRIVATE crosspoint_test_common xp_ffi_host)
add_test(NAME PageCodecDiff COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
  --dump $<TARGET_FILE:PageCodecDiff>)

target_sources(PageCodecDiff PRIVATE ${REPO_ROOT}/test/page_codec_diff/Serialization.cpp)
target_compile_definitions(PageCodecDiff PRIVATE XP_PAGE_CODEC_TEST=1)

target_sources(PageCodecDiff PRIVATE ${REPO_ROOT}/test/page_codec_diff/TextSerialization.cpp)
