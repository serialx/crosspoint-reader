function(reader_section_fixture target)
  foreach(feature app-test section-test)
    if(NOT XP_FFI_HOST_FEATURES MATCHES "(^|,)${feature}(,|$)")
      message(FATAL_ERROR "Reader Section fixtures require ${feature} in XP_FFI_HOST_FEATURES")
    endif()
  endforeach()
  set(fixture_dir ${REPO_ROOT}/test/reader_section)
  set(physical ${CMAKE_CURRENT_BINARY_DIR}/${target}SectionPhysical.cpp)
  add_custom_command(OUTPUT ${physical}
    COMMAND ${Python3_EXECUTABLE} ${fixture_dir}/generate_physical.py ${REPO_ROOT} ${physical} ${ARGN}
    DEPENDS ${fixture_dir}/generate_physical.py
      ${REPO_ROOT}/src/activities/reader/RustEpubResources.cpp
      ${REPO_ROOT}/test/reader_lifecycle/physical_defaults.h
    VERBATIM)
  target_sources(${target} PRIVATE ${fixture_dir}/ReaderSectionFixture.cpp ${physical})
  target_include_directories(${target} PRIVATE ${fixture_dir}
    ${REPO_ROOT}/test/reader_lifecycle ${REPO_ROOT}/test/ui_runtime)
  target_compile_options(${target} PRIVATE -UNDEBUG)
endfunction()

function(reader_section_trace_archive)
  if(NOT XP_FFI_HOST_FEATURES MATCHES "(^|,)chapter-trace(,|$)")
    message(FATAL_ERROR "Section trace archive requires chapter-trace in XP_FFI_HOST_FEATURES")
  endif()
  set(observer ${CMAKE_CURRENT_BINARY_DIR}/reader-section-observer)
  set(archive ${observer}/target/${XP_FFI_HOST_TRIPLE}/release/libxp_ffi.a)
  add_custom_target(reader_section_trace_build
    COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/test/reader_section/instrument.py ${REPO_ROOT} ${observer}
    COMMAND ${CMAKE_COMMAND} -E env --unset=MAKEFLAGS --unset=MFLAGS --unset=CARGO_MAKEFLAGS
      "CARGO_TARGET_DIR=${observer}/target" "CARGO_BUILD_JOBS=2"
      ${XP_FFI_CARGO} build --manifest-path ${observer}/rust/Cargo.toml
      -p xp-ffi --release --locked --target ${XP_FFI_HOST_TRIPLE}
      --features ${XP_FFI_HOST_FEATURES} ${XP_FFI_HOST_CARGO_ARGS}
    BYPRODUCTS ${archive} ${observer}/ReaderSectionObserver.cpp
    VERBATIM)
  add_library(reader_section_trace INTERFACE)
  add_dependencies(reader_section_trace reader_section_trace_build)
  target_sources(reader_section_trace INTERFACE ${REPO_ROOT}/test/cmake/xp_ffi_host.cpp
    ${observer}/ReaderSectionObserver.cpp)
  target_include_directories(reader_section_trace INTERFACE ${REPO_ROOT}/rust/crates/xp-ffi/include)
  target_link_libraries(reader_section_trace INTERFACE ${archive})
endfunction()
