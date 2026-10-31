cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(inflate_small C CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  get_filename_component(REPO_ROOT "../.." ABSOLUTE)
  include(../cmake/xp_ffi_host.cmake)
  enable_testing()
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_executable(InflateSmallProbe Probe.cpp ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp)
target_include_directories(InflateSmallProbe PRIVATE ${REPO_ROOT}/lib/InflateReader)
target_link_libraries(InflateSmallProbe PRIVATE xp_ffi_host)
add_test(NAME InflateSmallOracle COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/oracle.py
  --probe $<TARGET_FILE:InflateSmallProbe> --dict-probe $<TARGET_FILE:DictZipProbe> --output ${CMAKE_CURRENT_BINARY_DIR}/oracle)

add_executable(DictZipProbe DictProbe.cpp ${REPO_ROOT}/src/util/DictZip.cpp
  ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp)
target_include_directories(DictZipProbe PRIVATE stubs ${REPO_ROOT}/src/util
  ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/InflateReader)
target_link_libraries(DictZipProbe PRIVATE xp_ffi_host)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/DictionaryReadDefinition.inc
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/recovery.py
    --extract ${CMAKE_CURRENT_BINARY_DIR}/DictionaryReadDefinition.inc
  DEPENDS recovery.py ${REPO_ROOT}/src/util/Dictionary.cpp
  VERBATIM)
set(FONT_SDK ${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont)
add_executable(InflateRecoveryProbe RecoveryProbe.cpp ${CMAKE_CURRENT_BINARY_DIR}/DictionaryReadDefinition.inc
  ${REPO_ROOT}/src/util/DictZip.cpp ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp
  ${REPO_ROOT}/lib/EpdFont/FontDecompressor.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${FONT_SDK}/src/FontAlloc.c)
target_include_directories(InflateRecoveryProbe PRIVATE stubs
  ${CMAKE_CURRENT_BINARY_DIR} ${REPO_ROOT}/src/util ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/InflateReader ${REPO_ROOT}/lib/EpdFont ${REPO_ROOT}/lib/Utf8 ${FONT_SDK}/include)
target_link_libraries(InflateRecoveryProbe PRIVATE xp_ffi_host)
add_test(NAME InflateRecovery COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/recovery.py
  --probe $<TARGET_FILE:InflateRecoveryProbe> --output ${CMAKE_CURRENT_BINARY_DIR}/recovery)
