cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(sdfont_diff C CXX)
  set(CMAKE_CXX_STANDARD 20)
  get_filename_component(REPO_ROOT "../.." ABSOLUTE)
  include(${REPO_ROOT}/test/cmake/xp_ffi_host.cmake)
  enable_testing()
endif()
foreach(variant IN ITEMS oracle candidate)
  if(variant STREQUAL oracle)
    set(font_dir ${CMAKE_CURRENT_SOURCE_DIR}/oracle)
  else()
    set(font_dir ${REPO_ROOT}/lib/EpdFont)
  endif()
  add_executable(sdfont_${variant} Font.cpp ${font_dir}/SdCardFont.cpp
    ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp)
  target_include_directories(sdfont_${variant} PRIVATE ${font_dir}
    ${REPO_ROOT}/test/sd_card_font/stubs ${REPO_ROOT}/lib/EpdFont
    ${REPO_ROOT}/lib/Utf8 ${REPO_ROOT}/lib/Memory)
  target_link_libraries(sdfont_${variant} PRIVATE xp_ffi_host)
  if(variant STREQUAL candidate)
    target_compile_definitions(sdfont_${variant} PRIVATE SD_FONT_CANDIDATE)
  endif()
  add_executable(sdalloc_${variant} Allocation.cpp ${font_dir}/SdCardFont.cpp
    ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp)
  target_include_directories(sdalloc_${variant} PRIVATE ${font_dir}
    ${REPO_ROOT}/test/sd_card_font/stubs ${REPO_ROOT}/lib/EpdFont
    ${REPO_ROOT}/lib/Utf8 ${REPO_ROOT}/lib/Memory)
  target_link_libraries(sdalloc_${variant} PRIVATE xp_ffi_host)
  add_executable(sdname_${variant} Name.cpp ${font_dir}/SdCardFontRegistry.cpp)
  target_include_directories(sdname_${variant} PRIVATE ${font_dir}
    ${REPO_ROOT}/lib/EpdFont ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/test/library_builder/stubs)
  target_link_libraries(sdname_${variant} PRIVATE xp_ffi_host)
endforeach()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME SdFontDifferential COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
  --build-dir $<TARGET_FILE_DIR:sdfont_candidate>)
add_executable(sdfont_api Api.cpp)
target_link_libraries(sdfont_api PRIVATE xp_ffi_host)
target_include_directories(sdfont_api PRIVATE ${REPO_ROOT}/lib/EpdFont)
target_compile_options(sdfont_api PRIVATE -UNDEBUG)
add_test(NAME SdFontAbi COMMAND sdfont_api)
