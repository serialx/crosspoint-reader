cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(font_manager C CXX)
  set(CMAKE_CXX_STANDARD 20)
  get_filename_component(REPO_ROOT "../.." ABSOLUTE)
  enable_testing()
endif()
include(${REPO_ROOT}/test/cmake/xp_ffi_host.cmake)
add_executable(FontManagerOracle Oracle.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFontManager.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFontRegistry.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp)
target_include_directories(FontManagerOracle PRIVATE stubs ${REPO_ROOT}/test/font_cache/stubs
  ${REPO_ROOT}/lib/EpdFont ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/test/library_builder/stubs)
target_link_libraries(FontManagerOracle PRIVATE xp_ffi_host)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME FontManagerGolden COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check.py $<TARGET_FILE:FontManagerOracle>)
