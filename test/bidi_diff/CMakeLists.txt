if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.16)
  project(bidi_diff C CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
  include(${REPO_ROOT}/test/cmake/xp_ffi_host.cmake)
  enable_testing()
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(BIDI_REFERENCE ${CMAKE_CURRENT_BINARY_DIR}/reference)
file(MAKE_DIRECTORY ${BIDI_REFERENCE})
foreach(name minibidi.c minibidi.h BidiUtils.cpp bidiclasses.t bidi_pairs.t)
  execute_process(COMMAND git show 4436ccc228d23f94fd468723c8749607de1b09c4:lib/MiniBidi/${name}
    WORKING_DIRECTORY ${REPO_ROOT} OUTPUT_FILE ${BIDI_REFERENCE}/${name}
    COMMAND_ERROR_IS_FATAL ANY)
endforeach()
add_executable(BidiDiff
  main.cpp
  ${BIDI_REFERENCE}/minibidi.c
  ${BIDI_REFERENCE}/BidiUtils.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp)
target_include_directories(BidiDiff PRIVATE ${BIDI_REFERENCE}
  ${REPO_ROOT}/test/minibidi_arabic/stubs
  ${REPO_ROOT}/lib/MiniBidi ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/Utf8)
target_link_libraries(BidiDiff PRIVATE xp_ffi_host)
file(GLOB_RECURSE BIDI_PUBLIC_EPUBS CONFIGURE_DEPENDS "${REPO_ROOT}/test/*.epub")
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/corpus.txt
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/corpus.py ${CMAKE_CURRENT_BINARY_DIR}/corpus.txt
  DEPENDS corpus.py ${REPO_ROOT}/test/minibidi_arabic/MiniBidiArabicTest.cpp ${BIDI_PUBLIC_EPUBS}
  VERBATIM)
add_custom_target(BidiCorpus DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/corpus.txt)
add_dependencies(BidiDiff BidiCorpus)
add_test(NAME BidiDifferential COMMAND BidiDiff ${CMAKE_CURRENT_BINARY_DIR}/corpus.txt)
