# Each fake has its own TU: identical class names must never share a binary.
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generate.py
  ${CMAKE_CURRENT_BINARY_DIR}/generated COMMAND_ERROR_IS_FATAL ANY)
include(${CMAKE_CURRENT_BINARY_DIR}/generated/targets.cmake)

add_executable(HalContract_native ../sdfont_owner/native.cpp)
target_compile_definitions(HalContract_native PRIVATE HAL_CONTRACT_ONLY XP_SD_FONT_CATALOG)
target_compile_options(HalContract_native PRIVATE -UNDEBUG)
target_include_directories(HalContract_native PRIVATE
  ../sdfont_owner/stubs ${REPO_ROOT}/lib/hal ../sdfont_legacy
  ${REPO_ROOT}/lib/EpdFont ${REPO_ROOT}/rust/crates/xp-ffi/include)
add_test(NAME HalContract.native COMMAND HalContract_native)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS generate.py)
