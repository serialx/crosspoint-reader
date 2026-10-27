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
