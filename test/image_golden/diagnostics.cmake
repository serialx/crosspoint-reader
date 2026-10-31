# The reference is an external git archive of the listed da609bbf consumer libraries.
set(IMAGE_DIAGNOSTIC_REFERENCE "" CACHE PATH "Frozen da609bbf consumer export")
set(ANIMATEDGIF_SOURCE_DIR "" CACHE PATH "Pinned patched AnimatedGIF source")
if(IMAGE_DIAGNOSTIC_REFERENCE AND PNGDEC_REFERENCE_DIR AND ANIMATEDGIF_SOURCE_DIR)
  set(REF ${IMAGE_DIAGNOSTIC_REFERENCE})
  set(CONVERTERS ${REF}/lib/Epub/Epub/converters)
  set(PNGDEC ${PNGDEC_REFERENCE_DIR}/PNGdec/src)
  file(GLOB PNG_REF_C "${PNGDEC}/*.c")
  add_library(diagnostic_reference_code STATIC
    ${CONVERTERS}/PngToFramebufferConverter.cpp
    ${CONVERTERS}/GifToFramebufferConverter.cpp
    ${REPO_ROOT}/lib/Epub/Epub/converters/JpegToFramebufferConverter.cpp
    ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
    ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
    ${REF}/lib/PngToBmpConverter/PngToBmpConverter.cpp
    ${REF}/lib/GifToBmpConverter/GifToBmpConverter.cpp
    ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
    ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
    ${REPO_ROOT}/lib/miniz/src/InflateStream.cpp
    ${REF}/lib/PngZlib/PngZlib.cpp
    ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
    ${REPO_ROOT}/lib/miniz/src/MinizCompat.cpp
    ${PNGDEC}/PNGdec.cpp ${PNG_REF_C}
    ${ANIMATEDGIF_SOURCE_DIR}/src/AnimatedGIF.cpp
    ${DECODER_SOURCE_DIR}/JPEGDEC/src/JPEGDEC.cpp)
  target_include_directories(diagnostic_reference_code PUBLIC
    ${REPO_ROOT}/test/image_golden/stubs
    ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
    ${CONVERTERS} ${REF}/lib/PngToBmpConverter ${REF}/lib/GifToBmpConverter
    ${REF}/lib/GifSupport ${REF}/lib/PngZlib
    ${REPO_ROOT}/lib/miniz/src ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/GfxRenderer
    ${PNGDEC} ${ANIMATEDGIF_SOURCE_DIR}/src ${DECODER_SOURCE_DIR}/JPEGDEC/src)
  target_compile_definitions(diagnostic_reference_code PUBLIC __LINUX__ MAX_WIDTH=3072 PNG_MAX_BUFFERED_PIXELS=16416)
  target_link_libraries(diagnostic_reference_code PUBLIC xp_ffi_host)
  foreach(pair IN ITEMS "frame;Oracle.cpp" "png;PngBmpOracle.cpp" "gif;../gif_support/Oracle.cpp")
    list(GET pair 0 name)
    list(GET pair 1 source)
    add_executable(diagnostic_${name}_reference ${source})
    target_link_libraries(diagnostic_${name}_reference PRIVATE diagnostic_reference_code)
  endforeach()
endif()
