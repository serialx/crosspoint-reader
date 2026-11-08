# Host-only xp-ffi archive and ESP allocator adapters, shared by native suites.
if(TARGET xp_ffi_host)
  return()
endif()
find_program(XP_FFI_CARGO cargo REQUIRED)
find_program(XP_FFI_RUSTC rustc REQUIRED)
set(XP_FFI_HOST_FEATURES "chapter-test,chapter-trace,package-trace,app-test,section-test" CACHE STRING "Cargo features for the host xp-ffi archive")
set(XP_FFI_HOST_CARGO_ARGS "" CACHE STRING "Additional host cargo build arguments (CMake list)")
execute_process(COMMAND ${XP_FFI_RUSTC} -vV OUTPUT_VARIABLE XP_FFI_RUSTC_VERSION
  RESULT_VARIABLE XP_FFI_RUSTC_STATUS)
if(NOT XP_FFI_RUSTC_STATUS EQUAL 0)
  message(FATAL_ERROR "Cannot query rustc host target")
endif()
string(REGEX MATCH "host: ([^\n\r]+)" XP_FFI_HOST_MATCH "${XP_FFI_RUSTC_VERSION}")
set(XP_FFI_HOST_TRIPLE "${CMAKE_MATCH_1}")
if(NOT XP_FFI_HOST_TRIPLE)
  message(FATAL_ERROR "rustc did not report its host target")
endif()
get_filename_component(XP_FFI_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(XP_FFI_HOST_WORKSPACE "${XP_FFI_REPO_ROOT}/rust" CACHE PATH "Rust workspace used by host oracles")
set(XP_FFI_HOST_TARGET_DIR "${CMAKE_BINARY_DIR}/xp-ffi-host-target")
set(XP_FFI_HOST_ARCHIVE "${XP_FFI_HOST_TARGET_DIR}/${XP_FFI_HOST_TRIPLE}/release/libxp_ffi.a")
# The archive and the C++ translation IDs must come from the same generator run.
set(XP_FFI_HOST_I18N_DIR "${CMAKE_BINARY_DIR}/xp-ffi-host-i18n")
set(XP_FFI_HOST_I18N_HEADER "${XP_FFI_HOST_I18N_DIR}/I18nKeys.h")
set(XP_FFI_HOST_FEATURE_ARGS)
if(XP_FFI_HOST_FEATURES)
  list(APPEND XP_FFI_HOST_FEATURE_ARGS --features "${XP_FFI_HOST_FEATURES}")
endif()
add_custom_target(xp_ffi_host_build
  COMMAND ${CMAKE_COMMAND} -E env --unset=MAKEFLAGS --unset=MFLAGS --unset=CARGO_MAKEFLAGS "CARGO_TARGET_DIR=${XP_FFI_HOST_TARGET_DIR}"
    ${XP_FFI_CARGO} build --manifest-path ${XP_FFI_HOST_WORKSPACE}/Cargo.toml
    -p xp-ffi --release --locked --target ${XP_FFI_HOST_TRIPLE}
    ${XP_FFI_HOST_FEATURE_ARGS} ${XP_FFI_HOST_CARGO_ARGS}
  BYPRODUCTS ${XP_FFI_HOST_ARCHIVE}
  VERBATIM
)
# Publish the archive's own I18nKeys.h so C++ consumers never read a header that
# a firmware build happened to generate in lib/I18n. Cargo rewrites that header
# with the archive but gives CMake no file to depend on, so publication runs on
# every build and the script copies only when the bytes differ. xp_ffi_host carries
# the dependency, so every consumer compiles after publication.
add_custom_target(xp_ffi_host_i18n
  COMMAND ${CMAKE_COMMAND} -DARCHIVE=${XP_FFI_HOST_ARCHIVE} -DOUTPUT=${XP_FFI_HOST_I18N_HEADER}
    -P ${CMAKE_CURRENT_LIST_DIR}/generated_i18n.cmake
  DEPENDS xp_ffi_host_build ${CMAKE_CURRENT_LIST_DIR}/generated_i18n.cmake
  COMMENT "Publishing the host archive's generated I18nKeys.h"
  BYPRODUCTS ${XP_FFI_HOST_I18N_HEADER}
  VERBATIM
)
add_library(xp_ffi_host INTERFACE)
add_dependencies(xp_ffi_host xp_ffi_host_build xp_ffi_host_i18n)
target_sources(xp_ffi_host INTERFACE ${CMAKE_CURRENT_LIST_DIR}/xp_ffi_host.cpp)
target_include_directories(xp_ffi_host INTERFACE ${XP_FFI_HOST_I18N_DIR}
  ${XP_FFI_HOST_WORKSPACE}/crates/xp-ffi/include
  ${XP_FFI_REPO_ROOT}/test/reader_lifecycle)
target_link_libraries(xp_ffi_host INTERFACE ${XP_FFI_HOST_ARCHIVE})
