cmake_minimum_required(VERSION 3.16)
project(sdfont_owner C CXX)
set(CMAKE_CXX_STANDARD 20)
get_filename_component(REPO_ROOT "../.." ABSOLUTE)
include(${REPO_ROOT}/test/cmake/xp_ffi_host.cmake)
add_executable(sdfont_owner_oracle oracle.cpp
  ${REPO_ROOT}/test/sdfont_diff/oracle/SdCardFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp)
target_include_directories(sdfont_owner_oracle PRIVATE
  ${REPO_ROOT}/test/sdfont_diff/oracle ${REPO_ROOT}/test/sd_card_font/stubs
  ${REPO_ROOT}/lib/EpdFont ${REPO_ROOT}/lib/Utf8 ${REPO_ROOT}/lib/Memory)
target_link_libraries(sdfont_owner_oracle PRIVATE xp_ffi_host)
add_executable(sdfont_owner_physical native.cpp ${REPO_ROOT}/lib/hal/SdFontPhysical.cpp)
target_include_directories(sdfont_owner_physical PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs ${REPO_ROOT}/lib/hal ${REPO_ROOT}/rust/crates/xp-ffi/include)
