cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(vector_font C CXX)
  set(CMAKE_CXX_STANDARD 20)
  get_filename_component(REPO_ROOT "../.." ABSOLUTE)
  enable_testing()
endif()
include(${REPO_ROOT}/test/cmake/xp_ffi_host.cmake)
set(FONT_SDK ${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont)
file(GLOB FT_SOURCES ${FONT_SDK}/src/freetype/*.c)
add_executable(VectorFontOracle Oracle.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/test/vector_font/legacy/TtfEpdFont.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${FONT_SDK}/src/FtFont.cpp ${FONT_SDK}/src/Gsub.cpp ${FONT_SDK}/src/Gpos.cpp ${FT_SOURCES})
target_include_directories(VectorFontOracle PRIVATE legacy stubs ${REPO_ROOT}/test/font_cache/stubs
  ${REPO_ROOT}/lib/EpdFont ${REPO_ROOT}/lib/Utf8 ${FONT_SDK}/include
  ${FONT_SDK}/third_party/freetype/include ${FONT_SDK}/third_party/stb)
target_compile_definitions(VectorFontOracle PRIVATE BOARD_HAS_PSRAM=1 FREEINK_FONT_ENABLE_AUTOHINT=1)
target_compile_options(VectorFontOracle PRIVATE -fno-exceptions)
target_link_libraries(VectorFontOracle PRIVATE xp_ffi_host)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME VectorFontGolden COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check.py
  $<TARGET_FILE:VectorFontOracle> ${REPO_ROOT}/lib/EpdFont/builtinFonts/source/NotoSans/NotoSans-Regular.ttf)

# Standalone adapter for the production owner and frozen native oracle.
set(VECTOR_OWNER_TARGET "${CMAKE_BINARY_DIR}/vector-owner-target")
set(VECTOR_OWNER_ARCHIVE "${VECTOR_OWNER_TARGET}/${XP_FFI_HOST_TRIPLE}/release/libvector_owner_host.a")
add_custom_target(vector_owner_host_build
  COMMAND ${CMAKE_COMMAND} -E env --unset=MAKEFLAGS --unset=MFLAGS --unset=CARGO_MAKEFLAGS
    "CARGO_TARGET_DIR=${VECTOR_OWNER_TARGET}" ${XP_FFI_CARGO} build
    --manifest-path ${CMAKE_CURRENT_SOURCE_DIR}/owned/Cargo.toml --release --locked --offline
    --target ${XP_FFI_HOST_TRIPLE}
  BYPRODUCTS ${VECTOR_OWNER_ARCHIVE} VERBATIM)
add_executable(VectorFontOwnedDifferential owned/Differential.cpp frozen/TtfEpdFont.cpp
  ${REPO_ROOT}/lib/hal/VectorFontPhysical.cpp
  ${FONT_SDK}/src/FtFont.cpp ${FONT_SDK}/src/Gsub.cpp ${FONT_SDK}/src/Gpos.cpp ${FT_SOURCES})
add_dependencies(VectorFontOwnedDifferential vector_owner_host_build)
target_include_directories(VectorFontOwnedDifferential PRIVATE owned frozen stubs
  ${REPO_ROOT}/test/font_cache/stubs ${REPO_ROOT}/lib/EpdFont ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/test/sdfont_legacy/abi ${REPO_ROOT}/rust/crates/xp-ffi/include ${FONT_SDK}/include ${FONT_SDK}/third_party/freetype/include ${FONT_SDK}/third_party/stb)
target_compile_definitions(VectorFontOwnedDifferential PRIVATE XP_VECTOR_OWNER_TEST BOARD_HAS_PSRAM=1 FREEINK_FONT_ENABLE_AUTOHINT=1)
target_compile_options(VectorFontOwnedDifferential PRIVATE -fno-exceptions)
target_link_libraries(VectorFontOwnedDifferential PRIVATE ${VECTOR_OWNER_ARCHIVE})
add_test(NAME VectorFontOwnedDifferential COMMAND VectorFontOwnedDifferential
  ${REPO_ROOT}/lib/EpdFont/builtinFonts/source/NotoSans/NotoSans-Regular.ttf)
add_test(NAME VectorFontOwnedAbort COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/owned/check_abort.py
  $<TARGET_FILE:VectorFontOwnedDifferential> ${REPO_ROOT}/lib/EpdFont/builtinFonts/source/NotoSans/NotoSans-Regular.ttf)
add_test(NAME VectorFontFrozenInputs COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/owned/check_frozen.py)

include(manager/targets.cmake)
