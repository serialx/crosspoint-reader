cmake_minimum_required(VERSION 3.16)
project(png_zlib_tests C CXX)
get_filename_component(PNG_ZLIB_ROOT "../.." ABSOLUTE)
set(CMAKE_CXX_STANDARD 20)
include(${PNG_ZLIB_ROOT}/test/cmake/xp_ffi_host.cmake)
add_executable(png_zlib_test PngZlibTest.cpp
  ${PNG_ZLIB_ROOT}/lib/PngZlib/PngZlib.cpp
  ${PNG_ZLIB_ROOT}/lib/miniz/src/miniz_impl.c
  ${PNG_ZLIB_ROOT}/lib/miniz/src/MinizCompat.cpp)
target_include_directories(png_zlib_test PRIVATE
  ${PNG_ZLIB_ROOT}/lib/PngZlib ${PNG_ZLIB_ROOT}/lib/miniz/src)
target_link_libraries(png_zlib_test PRIVATE xp_ffi_host)
enable_testing()
add_test(NAME PngZlibStream COMMAND png_zlib_test)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME PatchPngdec COMMAND ${Python3_EXECUTABLE} ${PNG_ZLIB_ROOT}/test/test_patch_pngdec.py)

set(PNG_SOURCE_DIR "" CACHE PATH "Optional PNGdec library for differential probes")
if(PNG_SOURCE_DIR)
  file(GLOB PNG_PROBE_C "${PNG_SOURCE_DIR}/src/*.c")
  add_executable(png_decoder_probe DecoderProbe.cpp
    ${PNG_SOURCE_DIR}/src/PNGdec.cpp ${PNG_PROBE_C})
  target_compile_definitions(png_decoder_probe PRIVATE __LINUX__ PNG_MAX_BUFFERED_PIXELS=16416)
  target_include_directories(png_decoder_probe PRIVATE ${PNG_SOURCE_DIR}/src
    ${PNG_ZLIB_ROOT}/lib/PngZlib ${PNG_ZLIB_ROOT}/lib/miniz/src)
  if(EXISTS "${PNG_SOURCE_DIR}/src/inflate.c.disabled")
    target_sources(png_decoder_probe PRIVATE ${PNG_ZLIB_ROOT}/lib/PngZlib/PngZlib.cpp
      ${PNG_ZLIB_ROOT}/lib/miniz/src/miniz_impl.c
  ${PNG_ZLIB_ROOT}/lib/miniz/src/MinizCompat.cpp)
    target_link_libraries(png_decoder_probe PRIVATE xp_ffi_host)
    add_test(NAME PngInvalidHistory COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/test_invalid_history.py $<TARGET_FILE:png_decoder_probe>)
  endif()
endif()
