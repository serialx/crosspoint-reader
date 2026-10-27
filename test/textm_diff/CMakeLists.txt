cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(textm_diff C CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_BUILD_TYPE Release)
  get_filename_component(REPO_ROOT ../.. ABSOLUTE)
  include(../cmake/xp_ffi_host.cmake)
  enable_testing()
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract.py
  ${CMAKE_CURRENT_BINARY_DIR}/measurement.inc ${CMAKE_CURRENT_BINARY_DIR}/visual.inc
  COMMAND_ERROR_IS_FATAL ANY)
add_executable(TextmDiff main.cpp Renderer.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFont.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp ${REPO_ROOT}/lib/MiniBidi/minibidi.c)
target_include_directories(TextmDiff PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
  ${REPO_ROOT}/test/sd_card_font/stubs ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8 ${REPO_ROOT}/lib/MiniBidi ${REPO_ROOT}/lib/Memory)
target_compile_definitions(TextmDiff PRIVATE XP_FFI_HOST_FAULT_INJECTION)
target_compile_options(TextmDiff PRIVATE -O2 -fno-exceptions)
target_link_libraries(TextmDiff PRIVATE xp_ffi_host)
add_test(NAME TextmDiff COMMAND TextmDiff)

set(NATIVE_FONT_SDK ${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont)
file(GLOB NATIVE_FT_SOURCES ${NATIVE_FONT_SDK}/src/freetype/*.c)
add_executable(NativeCache NativeCache.cpp
  ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp ${REPO_ROOT}/lib/GfxRenderer/FontCacheManager.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFont.cpp ${REPO_ROOT}/lib/EpdFont/FontDecompressor.cpp
  ${REPO_ROOT}/lib/EpdFont/TtfEpdFont.cpp ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp ${REPO_ROOT}/lib/uzlib/src/tinflate.c
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp ${NATIVE_FONT_SDK}/src/FtFont.cpp
  ${NATIVE_FONT_SDK}/src/FontAlloc.c ${NATIVE_FONT_SDK}/src/Gsub.cpp ${NATIVE_FONT_SDK}/src/Gpos.cpp
  ${NATIVE_FT_SOURCES})
target_include_directories(NativeCache PRIVATE ../text_golden/stubs
  ${REPO_ROOT}/src ${REPO_ROOT}/lib/GfxRenderer ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/MiniBidi ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/InflateReader ${REPO_ROOT}/lib/uzlib/src
  ${NATIVE_FONT_SDK}/include ${NATIVE_FONT_SDK}/third_party/freetype/include ${NATIVE_FONT_SDK}/third_party/stb)
target_compile_definitions(NativeCache PRIVATE BOARD_HAS_PSRAM=1 FREEINK_FONT_ENABLE_AUTOHINT=1)
target_compile_options(NativeCache PRIVATE -ffunction-sections -fdata-sections -fno-exceptions)
if(APPLE)
  target_link_options(NativeCache PRIVATE -Wl,-dead_strip)
else()
  target_link_options(NativeCache PRIVATE -Wl,--gc-sections)
endif()
target_link_libraries(NativeCache PRIVATE xp_ffi_host)
execute_process(COMMAND ${Python3_EXECUTABLE} -c
  "import gzip,pathlib; pathlib.Path(r'${CMAKE_CURRENT_BINARY_DIR}/native.cpfont').write_bytes(gzip.decompress(pathlib.Path(r'${REPO_ROOT}/test/text_golden/fonts/NotoSans_14.cpfont.gz').read_bytes()))"
  COMMAND_ERROR_IS_FATAL ANY)
add_test(NAME NativeCache COMMAND NativeCache ${CMAKE_CURRENT_BINARY_DIR}/native.cpfont)

# Reuse the real renderer/font sources and host flags for the paragraph comparison.
get_target_property(NATIVE_PARAGRAPH_SOURCES NativeCache SOURCES)
list(REMOVE_ITEM NATIVE_PARAGRAPH_SOURCES NativeCache.cpp)
add_executable(NativeParagraph NativeParagraph.cpp FrozenRenderer.cpp ${NATIVE_PARAGRAPH_SOURCES})
target_include_directories(NativeParagraph PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
foreach(PROPERTY INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS LINK_OPTIONS)
  get_target_property(NATIVE_PROPERTY NativeCache ${PROPERTY})
  if(NATIVE_PROPERTY)
    set_property(TARGET NativeParagraph APPEND PROPERTY ${PROPERTY} ${NATIVE_PROPERTY})
  endif()
endforeach()
target_link_libraries(NativeParagraph PRIVATE xp_ffi_host)
add_test(NAME NativeParagraph COMMAND NativeParagraph ${CMAKE_CURRENT_BINARY_DIR}/native.cpfont
  ${REPO_ROOT}/lib/EpdFont/builtinFonts/source/NotoSans/NotoSans-Regular.ttf)
