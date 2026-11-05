cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(reader_children_tests CXX)
  set(CMAKE_CXX_STANDARD 20)
  enable_testing()
  get_filename_component(REPO_ROOT "../.." ABSOLUTE)
  include(${REPO_ROOT}/test/cmake/xp_ffi_host.cmake)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(READER_CHILDREN_CARGO cargo REQUIRED)
set(READER_CHILDREN_TARGET "${CMAKE_CURRENT_BINARY_DIR}/cargo")
set(READER_PERCENT_NATIVE "${CMAKE_CURRENT_BINARY_DIR}/percent_native.cpp")
add_custom_command(OUTPUT ${READER_PERCENT_NATIVE}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/prepare_percent.py ${READER_PERCENT_NATIVE}
  DEPENDS prepare_percent.py percent_native.cpp.in ../reader_lifecycle/extract.py
    ${REPO_ROOT}/src/activities/reader/EpubReaderPercentSelectionActivity.cpp
    ${REPO_ROOT}/src/components/UiAppHost.cpp ${REPO_ROOT}/src/util/ButtonNavigator.cpp
  VERBATIM)
add_executable(ReaderPercentNative ${READER_PERCENT_NATIVE}
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp)
target_include_directories(ReaderPercentNative PRIVATE ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include)
add_custom_target(reader_children_rust ALL
  COMMAND ${CMAKE_COMMAND} -E env "CARGO_TARGET_DIR=${READER_CHILDREN_TARGET}"
    ${READER_CHILDREN_CARGO} build --manifest-path ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml
    --locked --offline --bins
  BYPRODUCTS ${READER_CHILDREN_TARGET}/debug/percent-oracle ${READER_CHILDREN_TARGET}/debug/chapter-oracle
  VERBATIM)
add_test(NAME reader_children_ownership
  COMMAND ${CMAKE_COMMAND} -E env "CARGO_TARGET_DIR=${READER_CHILDREN_TARGET}"
    ${READER_CHILDREN_CARGO} test --manifest-path ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml
    --locked --offline --lib)
add_test(NAME reader_percent_native_parity
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/compare_percent.py
    --native $<TARGET_FILE:ReaderPercentNative> --rust ${READER_CHILDREN_TARGET}/debug/percent-oracle)

set(READER_CHAPTER_NATIVE "${CMAKE_CURRENT_BINARY_DIR}/chapter_native.cpp")
add_custom_command(OUTPUT ${READER_CHAPTER_NATIVE}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/prepare_chapters.py ${READER_CHAPTER_NATIVE}
  DEPENDS prepare_chapters.py chapter_native.cpp.in ../reader_lifecycle/book_cache_fixture.h
    ../reader_lifecycle/test_popup_resource.py
    ${REPO_ROOT}/src/activities/reader/EpubReaderChapterSelectionActivity.cpp
    ${REPO_ROOT}/src/activities/reader/RustEpubResources.cpp
    ${REPO_ROOT}/lib/Epub/Epub/PackagePlatform.cpp
  VERBATIM)
add_executable(ReaderChapterNative ${READER_CHAPTER_NATIVE})
target_link_libraries(ReaderChapterNative PRIVATE xp_ffi_host)
target_compile_definitions(ReaderChapterNative PRIVATE XP_FFI_HOST_FAULT_INJECTION)
target_compile_options(ReaderChapterNative PRIVATE -UNDEBUG)
target_include_directories(ReaderChapterNative PRIVATE ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/test/reader_lifecycle ${REPO_ROOT}/test/ui_runtime
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs)
add_test(NAME reader_chapter_native_parity
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/compare_chapters.py
    --native $<TARGET_FILE:ReaderChapterNative> --rust ${READER_CHILDREN_TARGET}/debug/chapter-oracle)
