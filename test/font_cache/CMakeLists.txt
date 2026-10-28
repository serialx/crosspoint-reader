cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(font_cache C CXX)
  set(CMAKE_CXX_STANDARD 20)
  get_filename_component(REPO_ROOT "../.." ABSOLUTE)
  enable_testing()
endif()
include(${REPO_ROOT}/test/cmake/xp_ffi_host.cmake)
add_executable(FontCacheOracle Oracle.cpp
  ${REPO_ROOT}/lib/EpdFont/FontDecompressor.cpp
  ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp)
target_include_directories(FontCacheOracle PRIVATE stubs
  ${REPO_ROOT}/test/text_golden/stubs ${REPO_ROOT}/test/sdfont_pool_diff/stubs
  ${REPO_ROOT}/lib/EpdFont ${REPO_ROOT}/lib/InflateReader ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont/include)
target_link_libraries(FontCacheOracle PRIVATE xp_ffi_host)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME FontCacheGolden COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check.py $<TARGET_FILE:FontCacheOracle>)
