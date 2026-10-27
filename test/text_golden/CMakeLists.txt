cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(text_golden C CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  get_filename_component(REPO_ROOT "../.." ABSOLUTE)
  include(../cmake/xp_ffi_host.cmake)
  enable_testing()
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(FONT_SDK ${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont)
file(GLOB FT_SOURCES ${FONT_SDK}/src/freetype/*.c)
set(INSTRUMENTED ${CMAKE_CURRENT_BINARY_DIR}/instrumented)
add_custom_command(OUTPUT ${INSTRUMENTED}/GfxRenderer.cpp ${INSTRUMENTED}/GfxRenderer.h
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/instrument.py ${REPO_ROOT} ${INSTRUMENTED}
  DEPENDS instrument.py ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.h
  VERBATIM)
add_executable(TextGolden TextGolden.cpp ${INSTRUMENTED}/GfxRenderer.cpp
  ${REPO_ROOT}/lib/GfxRenderer/FontCacheManager.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFont.cpp ${REPO_ROOT}/lib/EpdFont/FontDecompressor.cpp
  ${REPO_ROOT}/lib/EpdFont/TtfEpdFont.cpp
  ${REPO_ROOT}/lib/MiniBidi/RustBidi.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${FONT_SDK}/src/FtFont.cpp ${FONT_SDK}/src/FontAlloc.c ${FONT_SDK}/src/Gsub.cpp ${FONT_SDK}/src/Gpos.cpp
  ${FT_SOURCES})
target_include_directories(TextGolden PRIVATE ${INSTRUMENTED} stubs .
  ${REPO_ROOT}/src ${REPO_ROOT}/lib/GfxRenderer ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/MiniBidi ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/InflateReader
  ${FONT_SDK}/include ${FONT_SDK}/third_party/freetype/include ${FONT_SDK}/third_party/stb)
target_compile_definitions(TextGolden PRIVATE BOARD_HAS_PSRAM=1
  FREEINK_FONT_ENABLE_AUTOHINT=1)
target_compile_options(TextGolden PRIVATE -ffunction-sections -fdata-sections -fno-exceptions)
if(APPLE)
  target_link_options(TextGolden PRIVATE -Wl,-dead_strip)
else()
  target_link_options(TextGolden PRIVATE -Wl,--gc-sections)
endif()
target_link_libraries(TextGolden PRIVATE xp_ffi_host)
add_test(NAME TextGolden COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py check
  --binary $<TARGET_FILE:TextGolden> --work-dir ${CMAKE_CURRENT_BINARY_DIR}/check)
add_test(NAME TextDrawGolden COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/draw_run.py check
  --binary $<TARGET_FILE:TextGolden> --work-dir ${CMAKE_CURRENT_BINARY_DIR}/draw-check)
