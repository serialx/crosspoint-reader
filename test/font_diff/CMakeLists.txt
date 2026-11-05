cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(font_diff C CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
  enable_testing()
endif()
include(${REPO_ROOT}/test/cmake/xp_ffi_host.cmake)
execute_process(COMMAND git show 4436ccc228d23f94fd468723c8749607de1b09c4:lib/EpdFont/EpdFont.cpp
  WORKING_DIRECTORY ${REPO_ROOT} OUTPUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/EpdFontReference.cpp
  COMMAND_ERROR_IS_FATAL ANY)
add_executable(FontDiff FontDiff.cpp Abi.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/EpdFontReference.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFont.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp)
target_include_directories(FontDiff PRIVATE
  ${REPO_ROOT}/test/sd_card_font/stubs ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8 ${REPO_ROOT}/lib/Memory)
target_link_libraries(FontDiff PRIVATE xp_ffi_host)
target_compile_options(FontDiff PRIVATE -fno-exceptions)
target_compile_definitions(FontDiff PRIVATE XP_FFI_HOST_FAULT_INJECTION)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(FONT_DIFF_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/synthetic.cpfont")
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/make_fixture.py ${FONT_DIFF_FIXTURE}
  RESULT_VARIABLE FONT_DIFF_GENERATE_STATUS)
if(NOT FONT_DIFF_GENERATE_STATUS EQUAL 0)
  message(FATAL_ERROR "Cannot generate the synthetic font fixture")
endif()
add_test(NAME FontDiff COMMAND FontDiff ${FONT_DIFF_FIXTURE})

add_test(NAME FontBuiltinGolden COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_oracle.py $<TARGET_FILE:FontDiff>)
