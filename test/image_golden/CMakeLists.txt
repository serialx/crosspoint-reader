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
if(EXISTS "${DECODER_SOURCE_DIR}/PNGdec/src/inflate.c.disabled")
  list(APPEND PNG_C ${REPO_ROOT}/lib/PngZlib/PngZlib.cpp
    ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
    ${REPO_ROOT}/lib/miniz/src/MinizCompat.cpp)
endif()
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
  ${REPO_ROOT}/lib/PngZlib ${REPO_ROOT}/lib/miniz/src
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/lib/Epub/Epub/converters ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/GifSupport
  ${DECODER_SOURCE_DIR}/PNGdec/src ${DECODER_SOURCE_DIR}/JPEGDEC/src ${DECODER_SOURCE_DIR}/AnimatedGIF/src)
option(IMAGE_ASAN "Instrument host oracle" OFF)
if(IMAGE_ASAN)
  target_compile_options(image_oracle PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(image_oracle PRIVATE -fsanitize=address,undefined)
endif()

include(${REPO_ROOT}/test/cmake/xp_ffi_host.cmake)
include(${REPO_ROOT}/test/gif_support/oracle.cmake)
target_link_libraries(image_oracle PRIVATE xp_ffi_host)
add_executable(image_failures FailureTest.cpp)
target_link_libraries(image_failures PRIVATE xp_ffi_host)
target_compile_definitions(image_failures PRIVATE XP_FFI_HOST_FAULT_INJECTION)
enable_testing()
add_test(NAME ImagePipelineFailures COMMAND image_failures)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME ImagePipelineGoldens COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py $<TARGET_FILE:image_oracle>)

add_test(NAME ImagePipelineCaseKeys COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_run.py)
