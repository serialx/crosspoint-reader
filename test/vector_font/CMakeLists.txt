cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(vector_font C CXX)
  set(CMAKE_CXX_STANDARD 20)
  get_filename_component(REPO_ROOT "../.." ABSOLUTE)
  enable_testing()
endif()
include(${REPO_ROOT}/test/cmake/xp_ffi_host.cmake)
set(FONT_SDK ${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont)
file(GLOB FT_SOURCES ${FONT_SDK}/src/freetype/*.c)
add_executable(VectorFontOracle Oracle.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/EpdFont/TtfEpdFont.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${FONT_SDK}/src/FtFont.cpp ${FONT_SDK}/src/Gsub.cpp ${FONT_SDK}/src/Gpos.cpp ${FT_SOURCES})
target_include_directories(VectorFontOracle PRIVATE stubs ${REPO_ROOT}/test/font_cache/stubs
  ${REPO_ROOT}/lib/EpdFont ${REPO_ROOT}/lib/Utf8 ${FONT_SDK}/include
  ${FONT_SDK}/third_party/freetype/include ${FONT_SDK}/third_party/stb)
target_compile_definitions(VectorFontOracle PRIVATE BOARD_HAS_PSRAM=1 FREEINK_FONT_ENABLE_AUTOHINT=1)
target_compile_options(VectorFontOracle PRIVATE -fno-exceptions)
target_link_libraries(VectorFontOracle PRIVATE xp_ffi_host)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME VectorFontGolden COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check.py
  $<TARGET_FILE:VectorFontOracle> ${REPO_ROOT}/lib/EpdFont/builtinFonts/source/NotoSans/NotoSans-Regular.ttf)
