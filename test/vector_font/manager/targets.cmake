set(manager_oracle "${CMAKE_CURRENT_BINARY_DIR}/libmanager_oracle.a")
add_custom_target(vector_manager_oracle
  COMMAND ${XP_FFI_RUSTC} --edition=2021 --crate-type=staticlib -C opt-level=s -C panic=abort
    ${CMAKE_CURRENT_SOURCE_DIR}/manager/oracle.rs -o ${manager_oracle}
  BYPRODUCTS ${manager_oracle} VERBATIM)
# Separate archives keep the frozen manager on its native continuation protocol.
foreach(side parent candidate)
  set(manager_target "${CMAKE_CURRENT_BINARY_DIR}/manager-${side}-target")
  set(manager_archive "${manager_target}/${XP_FFI_HOST_TRIPLE}/release/libxp_ffi.a")
  set(manager_features "${XP_FFI_HOST_FEATURES},sd-font-catalog")
  if(side STREQUAL "candidate")
    string(APPEND manager_features ",vector-fonts")
  endif()
  add_custom_target(vector_manager_${side}_rust
    COMMAND ${CMAKE_COMMAND} -E env --unset=MAKEFLAGS --unset=MFLAGS --unset=CARGO_MAKEFLAGS
      "CARGO_TARGET_DIR=${manager_target}" ${XP_FFI_CARGO} build
      --manifest-path ${XP_FFI_HOST_WORKSPACE}/Cargo.toml -p xp-ffi --release --locked --offline
      --target ${XP_FFI_HOST_TRIPLE} --features "${manager_features}"
    BYPRODUCTS ${manager_archive} VERBATIM)
  if(side STREQUAL "parent")
    set(manager_source frozen/SdCardFontSystem.cpp)
    set(manager_include frozen)
  else()
    set(manager_include "${CMAKE_CURRENT_BINARY_DIR}/manager-source")
    file(MAKE_DIRECTORY ${manager_include})
    configure_file(${REPO_ROOT}/src/SdCardFontSystem.cpp ${manager_include}/SdCardFontSystem.cpp COPYONLY)
    configure_file(${REPO_ROOT}/src/SdCardFontSystem.h ${manager_include}/SdCardFontSystem.h COPYONLY)
    file(COPY ${REPO_ROOT}/rust/crates/xp-ffi/include/ DESTINATION ${manager_include})
    file(READ ${REPO_ROOT}/rust/crates/xp-ffi/include/xp_font_registry.h manager_header)
    string(REPLACE "!defined(XP_APP_TEST)" "1" manager_header "${manager_header}")
    file(WRITE ${manager_include}/xp_font_registry.h "${manager_header}")
    set(manager_source ${manager_include}/SdCardFontSystem.cpp)
  endif()
  add_executable(VectorManager_${side} manager/Manager.cpp ${manager_source} frozen/TtfEpdFont.cpp
    ${REPO_ROOT}/lib/hal/VectorFontPhysical.cpp ${REPO_ROOT}/src/ReaderFontSizes.cpp
    ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp
    ${REPO_ROOT}/test/cmake/xp_ffi_host.cpp
    ${FONT_SDK}/src/FtFont.cpp ${FONT_SDK}/src/Gsub.cpp ${FONT_SDK}/src/Gpos.cpp ${FT_SOURCES})
  add_dependencies(VectorManager_${side} vector_manager_${side}_rust xp_ffi_host_i18n vector_manager_oracle)
  target_include_directories(VectorManager_${side} PRIVATE manager ${manager_include} frozen
    ${REPO_ROOT}/test/sdfont_owner/system-stubs ${REPO_ROOT}/test/font_cache/stubs
    ${REPO_ROOT}/lib/EpdFont ${REPO_ROOT}/lib/GfxRenderer ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/Utf8
    ${REPO_ROOT}/src ${REPO_ROOT}/test/sdfont_legacy/abi ${REPO_ROOT}/rust/crates/xp-ffi/include
    ${XP_FFI_HOST_I18N_DIR} ${REPO_ROOT}/test/reader_lifecycle
    ${FONT_SDK}/include ${FONT_SDK}/third_party/freetype/include ${FONT_SDK}/third_party/stb)
  target_compile_definitions(VectorManager_${side} PRIVATE XP_APP_TEST XP_SD_FONT_CATALOG BOARD_HAS_PSRAM=1 FREEINK_FONT_ENABLE_AUTOHINT=1)
  if(side STREQUAL "candidate")
    target_compile_definitions(VectorManager_${side} PRIVATE XP_VECTOR_CANDIDATE)
  endif()
  target_compile_options(VectorManager_${side} PRIVATE -fno-exceptions -UNDEBUG)
  target_link_libraries(VectorManager_${side} PRIVATE ${manager_archive} ${manager_oracle})
endforeach()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${REPO_ROOT}/src/SdCardFontSystem.cpp ${REPO_ROOT}/src/SdCardFontSystem.h ${REPO_ROOT}/rust/crates/xp-ffi/include/xp_font_registry.h)
add_test(NAME VectorManagerDifferential COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/manager/check.py
  $<TARGET_FILE:VectorManager_parent> $<TARGET_FILE:VectorManager_candidate>
  ${REPO_ROOT}/lib/EpdFont/builtinFonts/source/NotoSans/NotoSans-Regular.ttf)
add_test(NAME VectorManagerLifetime COMMAND ${XP_FFI_CARGO} test
  --manifest-path ${CMAKE_CURRENT_SOURCE_DIR}/lifetime/Cargo.toml --offline --locked)
