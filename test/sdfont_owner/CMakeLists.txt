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
  ${REPO_ROOT}/test/sdfont_legacy ${REPO_ROOT}/lib/EpdFont ${REPO_ROOT}/lib/Utf8 ${REPO_ROOT}/lib/Memory)
target_link_libraries(sdfont_owner_oracle PRIVATE xp_ffi_host)
add_executable(sdfont_owner_physical native.cpp ${REPO_ROOT}/lib/hal/SdFontPhysical.cpp)
target_compile_definitions(sdfont_owner_physical PRIVATE XP_SD_FONT_CATALOG)
target_include_directories(sdfont_owner_physical PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs ${REPO_ROOT}/lib/hal ${REPO_ROOT}/test/sdfont_legacy ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/rust/crates/xp-ffi/include)
add_executable(sdfont_catalog_oracle catalog_oracle.cpp ${REPO_ROOT}/test/sdfont_legacy/SdCardFontRegistry.cpp)
target_compile_definitions(sdfont_catalog_oracle PRIVATE BOARD_HAS_PSRAM)
target_include_directories(sdfont_catalog_oracle PRIVATE
  ${REPO_ROOT}/test/font_registry/stubs ${REPO_ROOT}/test/sdfont_legacy ${REPO_ROOT}/lib/EpdFont ${REPO_ROOT}/lib/Memory)
target_link_libraries(sdfont_catalog_oracle PRIVATE xp_ffi_host)

add_executable(sdfont_owner_switch switch.cpp ${REPO_ROOT}/test/sdfont_legacy/SdCardFontManager.cpp)
target_include_directories(sdfont_owner_switch PRIVATE switch-stubs ${REPO_ROOT}/test/sdfont_legacy ${REPO_ROOT}/lib/EpdFont)
target_link_libraries(sdfont_owner_switch PRIVATE xp_ffi_host)
enable_testing()
add_test(NAME SdFontOwnerSwitch COMMAND sdfont_owner_switch)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
# This archive is isolated from the default ABI used by the full host suite.
set(CATALOG_TARGET "${CMAKE_CURRENT_BINARY_DIR}/catalog-target")
set(CATALOG_ARCHIVE "${CATALOG_TARGET}/${XP_FFI_HOST_TRIPLE}/release/libxp_ffi.a")
add_custom_target(xp_ffi_catalog_build
  COMMAND ${CMAKE_COMMAND} -E env --unset=MAKEFLAGS --unset=MFLAGS --unset=CARGO_MAKEFLAGS
    "CARGO_TARGET_DIR=${CATALOG_TARGET}" ${XP_FFI_CARGO} build
    --manifest-path ${XP_FFI_HOST_WORKSPACE}/Cargo.toml -p xp-ffi --release --locked --offline
    --target ${XP_FFI_HOST_TRIPLE} --features "${XP_FFI_HOST_FEATURES},sd-font-catalog"
  BYPRODUCTS ${CATALOG_ARCHIVE} VERBATIM)
add_dependencies(xp_ffi_catalog_build xp_ffi_host_i18n)
add_library(xp_ffi_catalog_host INTERFACE)
add_dependencies(xp_ffi_catalog_host xp_ffi_catalog_build)
target_compile_definitions(xp_ffi_catalog_host INTERFACE XP_APP_TEST XP_SD_FONT_CATALOG)
target_sources(xp_ffi_catalog_host INTERFACE ${REPO_ROOT}/test/cmake/xp_ffi_host.cpp)
target_include_directories(xp_ffi_catalog_host INTERFACE ${XP_FFI_HOST_I18N_DIR}
  ${REPO_ROOT}/test/font_cache/legacy ${REPO_ROOT}/test/sdfont_legacy ${REPO_ROOT}/rust/crates/xp-ffi/include
  ${REPO_ROOT}/test/reader_lifecycle)
target_link_libraries(xp_ffi_catalog_host INTERFACE ${CATALOG_ARCHIVE})
set(SYSTEM_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/system-source")
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/system_check.py
  --configure ${SYSTEM_FIXTURE} RESULT_VARIABLE SYSTEM_CONFIG_STATUS)
if(NOT SYSTEM_CONFIG_STATUS EQUAL 0)
  message(FATAL_ERROR "Cannot configure frozen SD system sources")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${REPO_ROOT}/src/SdCardFontSystem.cpp ${REPO_ROOT}/src/SdCardFontSystem.h)
foreach(side parent candidate)
  add_executable(sdfont_system_${side} system.cpp ${SYSTEM_FIXTURE}/${side}/SdCardFontSystem.cpp
    ${REPO_ROOT}/src/ReaderFontSizes.cpp ${REPO_ROOT}/test/sdfont_legacy/SdCardFontRegistry.cpp
    ${REPO_ROOT}/test/sdfont_legacy/SdCardFontManager.cpp ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp)
  target_include_directories(sdfont_system_${side} PRIVATE ${SYSTEM_FIXTURE}/${side} system-stubs
    ${REPO_ROOT}/test/sdfont_legacy ${REPO_ROOT}/lib/EpdFont ${REPO_ROOT}/lib/GfxRenderer ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/src)
  target_link_libraries(sdfont_system_${side} PRIVATE xp_ffi_catalog_host)
  target_compile_options(sdfont_system_${side} PRIVATE -UNDEBUG)
  if(side STREQUAL "parent")
    target_compile_definitions(sdfont_system_${side} PRIVATE XP_SYSTEM_PARENT)
  endif()
endforeach()
add_test(NAME SdFontSystemSwitch COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/system_check.py
  --parent $<TARGET_FILE:sdfont_system_parent> --candidate $<TARGET_FILE:sdfont_system_candidate>)

add_test(NAME SdFontCatalogFrontDoor
  COMMAND ${XP_FFI_CARGO} test --manifest-path ${REPO_ROOT}/rust/Cargo.toml -p xp-core
    --features sd-font-catalog --test font_owner_binding --offline --locked)
set_tests_properties(SdFontCatalogFrontDoor PROPERTIES WORKING_DIRECTORY ${REPO_ROOT}/rust)
add_test(NAME SdFontXtcOrder
  COMMAND ${XP_FFI_CARGO} test --manifest-path ${REPO_ROOT}/rust/Cargo.toml -p xp-ui
    --features sd-font-catalog --lib synchronous_font_effects_keep_entry_command_order_and_failures --offline --locked)
set_tests_properties(SdFontXtcOrder PROPERTIES WORKING_DIRECTORY ${REPO_ROOT}/rust)
