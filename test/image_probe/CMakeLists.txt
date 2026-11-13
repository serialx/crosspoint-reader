# Use the firmware JPEGDEC pin and its existing patches, independent of PlatformIO setup.
include(FetchContent)
FetchContent_Declare(reader_probe_jpegdec
  GIT_REPOSITORY https://github.com/bitbank2/JPEGDEC.git
  GIT_TAG 86282979224c8a32fd51e091ed5a35b0c699a52b
  GIT_SHALLOW FALSE
  SOURCE_SUBDIR host-source-only
  PATCH_COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/patch_dependency.py
    <SOURCE_DIR> ${REPO_ROOT}/scripts/jpegdec_patches)
FetchContent_MakeAvailable(reader_probe_jpegdec)
set(PROBE_GOLDEN ${REPO_ROOT}/test/image_golden)
set(PROBE_CONVERTERS ${REPO_ROOT}/lib/Epub/Epub/converters)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ReaderImageLeaves.cpp
  COMMAND ${Python3_EXECUTABLE} ${PROBE_GOLDEN}/reader_leaves.py
    ${REPO_ROOT}/lib/Epub/Epub/blocks/ImageBlock.cpp ${CMAKE_CURRENT_BINARY_DIR}/ReaderImageLeaves.cpp
  DEPENDS ${PROBE_GOLDEN}/reader_leaves.py ${REPO_ROOT}/lib/Epub/Epub/blocks/ImageBlock.cpp VERBATIM)
add_executable(reader_probe_oracle ${PROBE_GOLDEN}/Oracle.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/ReaderImageLeaves.cpp
  ${PROBE_CONVERTERS}/PngToFramebufferConverter.cpp
  ${PROBE_CONVERTERS}/JpegToFramebufferConverter.cpp
  ${PROBE_CONVERTERS}/GifToFramebufferConverter.cpp
  ${PROBE_CONVERTERS}/ImageToFramebufferDecoder.cpp
  ${PROBE_CONVERTERS}/ImageDimsProbe.cpp
  ${PROBE_CONVERTERS}/ImageDecoderFactory.cpp
  ${reader_probe_jpegdec_SOURCE_DIR}/src/JPEGDEC.cpp)
target_compile_definitions(reader_probe_oracle PRIVATE __LINUX__ READER_IMAGE_ORACLE XP_FFI_HOST_FAULT_INJECTION)
target_include_directories(reader_probe_oracle PRIVATE ${PROBE_GOLDEN}/stubs
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs ${PROBE_CONVERTERS}
  ${REPO_ROOT}/lib/GfxRenderer ${REPO_ROOT}/lib/PngDiagnostics
  ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/GifSupport ${reader_probe_jpegdec_SOURCE_DIR}/src)
target_link_libraries(reader_probe_oracle PRIVATE xp_ffi_host)
add_test(NAME ReaderImageProbeCorpus COMMAND ${Python3_EXECUTABLE}
  ${PROBE_GOLDEN}/reader_probe_corpus.py $<TARGET_FILE:reader_probe_oracle>)
