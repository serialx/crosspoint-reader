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
add_custom_command(OUTPUT ${INSTRUMENTED}/GfxRenderer.cpp ${INSTRUMENTED}/GfxRenderer.h ${INSTRUMENTED}/RustUiRenderer.cpp
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/instrument.py ${REPO_ROOT} ${INSTRUMENTED}
  DEPENDS instrument.py ${REPO_ROOT}/lib/GfxRenderer/RustUiRenderer.cpp ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.h
  VERBATIM)
add_executable(TextGolden TextGolden.cpp ${INSTRUMENTED}/GfxRenderer.cpp

  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/test/sdfont_legacy/SdCardFont.cpp ${REPO_ROOT}/test/font_cache/legacy/FontDecompressor.cpp
  ${REPO_ROOT}/test/vector_font/legacy/TtfEpdFont.cpp
  ${REPO_ROOT}/lib/MiniBidi/RustBidi.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${FONT_SDK}/src/FtFont.cpp ${FONT_SDK}/src/FontAlloc.c ${FONT_SDK}/src/Gsub.cpp ${FONT_SDK}/src/Gpos.cpp
  ${FT_SOURCES})
target_include_directories(TextGolden PRIVATE ${INSTRUMENTED} stubs .
  ${REPO_ROOT}/src ${REPO_ROOT}/lib/GfxRenderer ${REPO_ROOT}/test/sdfont_legacy ${REPO_ROOT}/lib/EpdFont
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

# Exercise the real font/resource bridge with full-frame and 16-row targets.
get_target_property(TEXT_GOLDEN_SOURCES TextGolden SOURCES)
list(REMOVE_ITEM TEXT_GOLDEN_SOURCES TextGolden.cpp)
add_executable(UiSceneTarget UiSceneTarget.cpp ${TEXT_GOLDEN_SOURCES}
  ${INSTRUMENTED}/RustUiRenderer.cpp
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp)
target_include_directories(UiSceneTarget PRIVATE $<TARGET_PROPERTY:TextGolden,INCLUDE_DIRECTORIES>
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include)
target_compile_definitions(UiSceneTarget PRIVATE $<TARGET_PROPERTY:TextGolden,COMPILE_DEFINITIONS>)
target_compile_options(UiSceneTarget PRIVATE $<TARGET_PROPERTY:TextGolden,COMPILE_OPTIONS> -UNDEBUG)
target_link_options(UiSceneTarget PRIVATE $<TARGET_PROPERTY:TextGolden,LINK_OPTIONS>)
target_link_libraries(UiSceneTarget PRIVATE xp_ffi_host)
add_test(NAME UiSceneTarget COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/ui_scene_target.py $<TARGET_FILE:UiSceneTarget>)

set(LIBRARY_HEADER_TEST ${REPO_ROOT}/test/ui_header)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/library_methods.inc
  COMMAND ${Python3_EXECUTABLE} ${LIBRARY_HEADER_TEST}/library_methods.py ${REPO_ROOT}
    ${CMAKE_CURRENT_BINARY_DIR}/library_methods.inc
  DEPENDS ${LIBRARY_HEADER_TEST}/library_methods.py ${LIBRARY_HEADER_TEST}/native_oracle/library-paint-header.txt
    ${REPO_ROOT}/src/activities/library/LibraryListActivity.cpp VERBATIM)
add_executable(LibraryHeaderPixels ${LIBRARY_HEADER_TEST}/library_pixels.cpp ${TEXT_GOLDEN_SOURCES}
  ${CMAKE_CURRENT_BINARY_DIR}/library_methods.inc ${REPO_ROOT}/src/components/UiHeader.cpp
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp)
target_include_directories(LibraryHeaderPixels PRIVATE $<TARGET_PROPERTY:TextGolden,INCLUDE_DIRECTORIES>
  ${CMAKE_CURRENT_BINARY_DIR} ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include
  ${REPO_ROOT}/freeink-sdk/libs/assets/Icons/include)
target_compile_definitions(LibraryHeaderPixels PRIVATE $<TARGET_PROPERTY:TextGolden,COMPILE_DEFINITIONS>
  LIBRARY_HEADER_FAULT_TEST=1)
target_compile_options(LibraryHeaderPixels PRIVATE $<TARGET_PROPERTY:TextGolden,COMPILE_OPTIONS> -UNDEBUG)
target_link_options(LibraryHeaderPixels PRIVATE $<TARGET_PROPERTY:TextGolden,LINK_OPTIONS>)
target_link_libraries(LibraryHeaderPixels PRIVATE xp_ffi_host)
add_test(NAME LibraryHeaderPixels COMMAND ${Python3_EXECUTABLE} ${LIBRARY_HEADER_TEST}/library_pixels.py
  $<TARGET_FILE:LibraryHeaderPixels>)

# Native status drawing remains the oracle for the Rust EPUB composer.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/status_spec.inc ${CMAKE_CURRENT_BINARY_DIR}/status_native.inc
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/status_methods.py ${REPO_ROOT} ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS status_methods.py ${REPO_ROOT}/test/reader_lifecycle/test_popup_resource.py
    ${REPO_ROOT}/src/CrossPointSettings.h ${REPO_ROOT}/src/components/themes/BaseTheme.cpp
    ${REPO_ROOT}/src/components/themes/lyra/LyraTheme.cpp ${REPO_ROOT}/src/components/UITheme.cpp
    ${REPO_ROOT}/src/activities/reader/RustEpubResources.cpp VERBATIM)
add_executable(ReaderStatusPixels StatusPixels.cpp ${TEXT_GOLDEN_SOURCES}
  ${CMAKE_CURRENT_BINARY_DIR}/status_spec.inc ${CMAKE_CURRENT_BINARY_DIR}/status_native.inc)
target_include_directories(ReaderStatusPixels BEFORE PRIVATE status_stubs ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(ReaderStatusPixels PRIVATE $<TARGET_PROPERTY:TextGolden,INCLUDE_DIRECTORIES>)
target_compile_definitions(ReaderStatusPixels PRIVATE $<TARGET_PROPERTY:TextGolden,COMPILE_DEFINITIONS>
  XP_FFI_HOST_FAULT_INJECTION TEST_DISPLAY_WIDTH=528 TEST_DISPLAY_HEIGHT=792)
target_compile_options(ReaderStatusPixels PRIVATE $<TARGET_PROPERTY:TextGolden,COMPILE_OPTIONS> -UNDEBUG)
target_link_options(ReaderStatusPixels PRIVATE $<TARGET_PROPERTY:TextGolden,LINK_OPTIONS>)
target_link_libraries(ReaderStatusPixels PRIVATE xp_ffi_host)
add_test(NAME ReaderStatusPixels COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/status_pixels.py
  $<TARGET_FILE:ReaderStatusPixels>)
add_test(NAME ReaderStatusLayout COMMAND ${XP_FFI_CARGO} test --manifest-path ${REPO_ROOT}/rust/Cargo.toml
  -p xp-ui --test reader_status --offline --locked)
set_tests_properties(ReaderStatusLayout PROPERTIES WORKING_DIRECTORY ${REPO_ROOT}/rust)

add_test(NAME ReaderStatusDispatch COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/status_dispatch.py
  ${REPO_ROOT})
