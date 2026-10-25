# Host-only xp-ffi archive and ESP allocator adapters, shared by native suites.
if(TARGET xp_ffi_host)
  return()
endif()
find_program(XP_FFI_CARGO cargo REQUIRED)
find_program(XP_FFI_RUSTC rustc REQUIRED)
set(XP_FFI_HOST_FEATURES "" CACHE STRING "Cargo features for the host xp-ffi archive")
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
set(XP_FFI_HOST_TARGET_DIR "${CMAKE_BINARY_DIR}/xp-ffi-host-target")
set(XP_FFI_HOST_ARCHIVE "${XP_FFI_HOST_TARGET_DIR}/${XP_FFI_HOST_TRIPLE}/release/libxp_ffi.a")
set(XP_FFI_HOST_FEATURE_ARGS)
if(XP_FFI_HOST_FEATURES)
  list(APPEND XP_FFI_HOST_FEATURE_ARGS --features "${XP_FFI_HOST_FEATURES}")
endif()
add_custom_target(xp_ffi_host_build
  COMMAND ${CMAKE_COMMAND} -E env --unset=MAKEFLAGS --unset=MFLAGS --unset=CARGO_MAKEFLAGS "CARGO_TARGET_DIR=${XP_FFI_HOST_TARGET_DIR}"
    ${XP_FFI_CARGO} build --manifest-path ${XP_FFI_REPO_ROOT}/rust/Cargo.toml
    -p xp-ffi --release --target ${XP_FFI_HOST_TRIPLE}
    ${XP_FFI_HOST_FEATURE_ARGS} ${XP_FFI_HOST_CARGO_ARGS}
  BYPRODUCTS ${XP_FFI_HOST_ARCHIVE}
  VERBATIM
)
add_library(xp_ffi_host INTERFACE)
add_dependencies(xp_ffi_host xp_ffi_host_build)
target_sources(xp_ffi_host INTERFACE ${CMAKE_CURRENT_LIST_DIR}/xp_ffi_host.cpp)
target_include_directories(xp_ffi_host INTERFACE ${XP_FFI_REPO_ROOT}/rust/crates/xp-ffi/include)
target_link_libraries(xp_ffi_host INTERFACE ${XP_FFI_HOST_ARCHIVE})
