cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(reader_children_tests C CXX)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
  set(CMAKE_CXX_STANDARD 20)
  enable_testing()
  get_filename_component(REPO_ROOT "../.." ABSOLUTE)
  include(${REPO_ROOT}/test/cmake/xp_ffi_host.cmake)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME reader_native_oracle_include_paths
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/test/cmake/check_native_oracle_includes.py
    ${CMAKE_BINARY_DIR}/compile_commands.json)
foreach(language c cpp)
  add_executable(reader_child_leaf_abi_${language} leaf_contract.${language})
  target_include_directories(reader_child_leaf_abi_${language} PRIVATE ${REPO_ROOT}/rust/crates/xp-ffi/include)
  add_test(NAME reader_child_leaf_abi_${language} COMMAND reader_child_leaf_abi_${language})
endforeach()
find_program(READER_CHILDREN_CARGO cargo REQUIRED)
set(READER_CHILDREN_TARGET "${CMAKE_CURRENT_BINARY_DIR}/cargo")
set(READER_PERCENT_NATIVE "${CMAKE_CURRENT_BINARY_DIR}/percent_native.cpp")
add_custom_command(OUTPUT ${READER_PERCENT_NATIVE}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/prepare_percent.py ${READER_PERCENT_NATIVE}
  DEPENDS prepare_percent.py percent_native.cpp.in ../reader_lifecycle/extract.py
    ${CMAKE_CURRENT_SOURCE_DIR}/native_oracle/src/activities/reader/EpubReaderPercentSelectionActivity.cpp.txt
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
    ${CMAKE_CURRENT_SOURCE_DIR}/native_oracle/src/activities/reader/EpubReaderChapterSelectionActivity.cpp.txt
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

if(NOT TARGET TextGolden)
  add_subdirectory(${REPO_ROOT}/test/text_golden ${CMAKE_BINARY_DIR}/text_golden)
endif()
set(READER_PERCENT_DRAWING ${CMAKE_CURRENT_BINARY_DIR}/percent_drawing.cpp)
file(GLOB READER_DRAWING_TRANSLATIONS CONFIGURE_DEPENDS ${REPO_ROOT}/lib/I18n/translations/*.yaml)
add_custom_command(OUTPUT ${READER_PERCENT_DRAWING} ${CMAKE_CURRENT_BINARY_DIR}/percent_i18n_keys.h
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/prepare_percent_drawing.py ${READER_PERCENT_DRAWING}
  DEPENDS prepare_percent_drawing.py percent_drawing.cpp.in ../reader_lifecycle/extract.py
    ${REPO_ROOT}/scripts/gen_i18n.py ${READER_DRAWING_TRANSLATIONS}
    ${REPO_ROOT}/src/components/UiSliderDialog.h ${REPO_ROOT}/src/components/UiAppHelpers.h
    ${REPO_ROOT}/src/components/UIThemeTokens.h
    ${CMAKE_CURRENT_SOURCE_DIR}/native_oracle/src/activities/reader/EpubReaderPercentSelectionActivity.cpp.txt
    ${REPO_ROOT}/src/activities/util/UtilityActivity.cpp
    ${REPO_ROOT}/src/activities/reader/RustReaderDrawing.cpp
    ${REPO_ROOT}/src/activities/reader/ReaderChromeListActivity.cpp
    ${REPO_ROOT}/test/ui_runtime/native_oracle/ReaderThemeSampling.txt
    ${REPO_ROOT}/test/ui_runtime/native_oracle/manifest.json
    ${REPO_ROOT}/src/components/themes/BaseTheme.cpp
    ${REPO_ROOT}/src/components/themes/lyra/LyraTheme.cpp
    ${REPO_ROOT}/src/components/themes/roundedraff/RoundedRaffTheme.cpp
  VERBATIM)
get_target_property(READER_DRAWING_SOURCES TextGolden SOURCES)
list(FILTER READER_DRAWING_SOURCES EXCLUDE REGEX "TextGolden.cpp$")
set_source_files_properties(${CMAKE_BINARY_DIR}/text_golden/instrumented/RustUiRenderer.cpp PROPERTIES GENERATED TRUE)
add_executable(ReaderPercentDrawing ${READER_PERCENT_DRAWING} ${READER_DRAWING_SOURCES}
  ${CMAKE_BINARY_DIR}/text_golden/instrumented/RustUiRenderer.cpp
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp)
target_include_directories(ReaderPercentDrawing PRIVATE $<TARGET_PROPERTY:TextGolden,INCLUDE_DIRECTORIES>
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include ${REPO_ROOT}/freeink-sdk/libs/assets/Icons/include ${REPO_ROOT}/test/text_golden
  ${REPO_ROOT}/test/ui_runtime ${REPO_ROOT}/lib/I18n)
target_compile_definitions(ReaderPercentDrawing PRIVATE $<TARGET_PROPERTY:TextGolden,COMPILE_DEFINITIONS>)
target_compile_options(ReaderPercentDrawing PRIVATE $<TARGET_PROPERTY:TextGolden,COMPILE_OPTIONS> -UNDEBUG)
target_link_options(ReaderPercentDrawing PRIVATE $<TARGET_PROPERTY:TextGolden,LINK_OPTIONS>)
target_link_libraries(ReaderPercentDrawing PRIVATE xp_ffi_host)
add_dependencies(ReaderPercentDrawing TextGolden)
add_test(NAME reader_percent_native_drawing COMMAND ReaderPercentDrawing)

set(READER_CHAPTER_GUARD "${CMAKE_CURRENT_BINARY_DIR}/chapter_guard.cpp")
add_custom_command(OUTPUT ${READER_CHAPTER_GUARD}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/prepare_chapters.py ${READER_CHAPTER_GUARD} guard
  DEPENDS prepare_chapters.py chapter_guard.cpp.in chapter_native.cpp.in ../reader_lifecycle/book_cache_fixture.h
    ../reader_lifecycle/test_popup_resource.py
    ${REPO_ROOT}/src/activities/reader/RustEpubResources.cpp
    ${REPO_ROOT}/lib/Epub/Epub/PackagePlatform.cpp
  VERBATIM)
add_executable(ReaderChapterGuard ${READER_CHAPTER_GUARD})
target_link_libraries(ReaderChapterGuard PRIVATE xp_ffi_host)
target_compile_definitions(ReaderChapterGuard PRIVATE XP_FFI_HOST_FAULT_INJECTION)
target_compile_options(ReaderChapterGuard PRIVATE -UNDEBUG)
target_include_directories(ReaderChapterGuard PRIVATE ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/test/reader_lifecycle ${REPO_ROOT}/test/ui_runtime
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs)
add_test(NAME reader_chapter_resource_guard COMMAND ReaderChapterGuard ${CMAKE_CURRENT_BINARY_DIR}/guard-book-fixture)

add_test(NAME reader_owned_app_children
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_owned_app.py --archive ${XP_FFI_HOST_ARCHIVE})

add_test(NAME reader_toolbar_owned_parity
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_toolbar_parity.py --archive ${XP_FFI_HOST_ARCHIVE})
