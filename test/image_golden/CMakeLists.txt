cmake_minimum_required(VERSION 3.16)
project(image_golden C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_BUILD_TYPE Release)
get_filename_component(REPO_ROOT "../.." ABSOLUTE)
set(DECODER_SOURCE_DIR "" CACHE PATH "Existing patched PlatformIO libdeps directory")
foreach(lib PNGdec JPEGDEC AnimatedGIF)
  if(NOT EXISTS "${DECODER_SOURCE_DIR}/${lib}/src/${lib}.cpp")
    message(FATAL_ERROR "Set DECODER_SOURCE_DIR to patched firmware libdeps")
  endif()
endforeach()
file(GLOB PNG_C "${DECODER_SOURCE_DIR}/PNGdec/src/*.c")
add_executable(image_oracle Oracle.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/PngToFramebufferConverter.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/JpegToFramebufferConverter.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/GifToFramebufferConverter.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${DECODER_SOURCE_DIR}/PNGdec/src/PNGdec.cpp ${PNG_C}
  ${DECODER_SOURCE_DIR}/JPEGDEC/src/JPEGDEC.cpp
  ${DECODER_SOURCE_DIR}/AnimatedGIF/src/AnimatedGIF.cpp)
target_compile_definitions(image_oracle PRIVATE __LINUX__ MAX_WIDTH=3072 PNG_MAX_BUFFERED_PIXELS=16416)
target_include_directories(image_oracle PRIVATE stubs
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/lib/Epub/Epub/converters ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/GifSupport
  ${DECODER_SOURCE_DIR}/PNGdec/src ${DECODER_SOURCE_DIR}/JPEGDEC/src ${DECODER_SOURCE_DIR}/AnimatedGIF/src)
option(IMAGE_ASAN "Instrument host oracle" OFF)
if(IMAGE_ASAN)
  target_compile_options(image_oracle PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(image_oracle PRIVATE -fsanitize=address,undefined)
endif()
