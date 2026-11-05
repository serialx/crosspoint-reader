find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME reader_lifecycle_oracle
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_lifecycle_session.py
)

foreach(mode off on)
  add_test(NAME reader_render_oracle_${mode}
    COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
      ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_render_session.py
      --mode ${mode})
endforeach()

add_executable(ReaderLifecycleAbi abi.cpp)
target_link_libraries(ReaderLifecycleAbi PRIVATE crosspoint_test_common)
target_compile_options(ReaderLifecycleAbi PRIVATE -UNDEBUG)
add_test(NAME reader_lifecycle_abi
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_lifecycle_boundary.py
    --repo ${REPO_ROOT} --archive ${XP_FFI_HOST_ARCHIVE}
    --original-executable $<TARGET_FILE:ReaderLifecycleAbi>)

add_test(NAME reader_result_tag_contract
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_result_tags.py
    --repo ${REPO_ROOT} --compiler ${CMAKE_CXX_COMPILER})

foreach(mode blocked failed)
  add_test(NAME reader_render_entry_${mode}
    COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
      ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_render_entry_gate.py --mode ${mode})
endforeach()

add_test(NAME reader_overlay_backup_concurrency
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_overlay_backup_concurrency.py)

add_test(NAME reader_popup_concurrency
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_popup_concurrency.py)

add_test(NAME reader_overlay_install_rejection
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_overlay_install_rejection.py)

add_test(NAME reader_rejected_input_session
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_rejected_input_session.py)

add_test(NAME reader_more_actions_session
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_more_actions_session.py)

set(INPUT_METHODS ${CMAKE_CURRENT_BINARY_DIR}/input_methods.inc)
add_custom_command(OUTPUT ${INPUT_METHODS}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract_input.py ${REPO_ROOT} ${INPUT_METHODS}
  DEPENDS extract_input.py extract.py ${REPO_ROOT}/src/activities/reader/ReaderUtils.h
  VERBATIM)
add_executable(ReaderInputOracle input_oracle.cpp ${INPUT_METHODS})
target_include_directories(ReaderInputOracle PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${REPO_ROOT}/src/activities/reader)
target_link_libraries(ReaderInputOracle PRIVATE crosspoint_test_common)
add_test(NAME reader_input_oracle
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_shared_input_nav.py
    $<TARGET_FILE:ReaderInputOracle> $<TARGET_FILE:ReaderNavOracle> --mode input
    --repo ${REPO_ROOT})

add_test(NAME reader_base_input_wrappers
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_input_session.py $<TARGET_FILE:ReaderInputOracle>)

add_executable(ReaderNavOracle nav_oracle.cpp)
target_compile_definitions(ReaderNavOracle PRIVATE READER_NAV_RUST=1)
target_link_libraries(ReaderNavOracle PRIVATE crosspoint_test_common)
target_include_directories(ReaderNavOracle PRIVATE ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include)
add_test(NAME reader_nav_oracle
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_shared_input_nav.py
    $<TARGET_FILE:ReaderInputOracle> $<TARGET_FILE:ReaderNavOracle> --mode nav
    --repo ${REPO_ROOT})

add_test(NAME reader_loop_oracle
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_loop_session.py)
add_test(NAME reader_loop_oracle_release
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_loop_session.py --release)

set(BASE_METHODS ${CMAKE_CURRENT_BINARY_DIR}/base_methods.inc)
add_custom_command(OUTPUT ${BASE_METHODS} ${CMAKE_CURRENT_BINARY_DIR}/base_action.inc
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract_base.py ${REPO_ROOT} ${BASE_METHODS}
  DEPENDS extract_base.py extract.py ${REPO_ROOT}/src/activities/reader/ReaderActivity.cpp
    ${REPO_ROOT}/src/activities/reader/EndOfBookOptions.h
  VERBATIM)
add_executable(ReaderBaseOracle base_oracle.cpp ${BASE_METHODS})
target_link_libraries(ReaderBaseOracle PRIVATE crosspoint_test_common)
target_include_directories(ReaderBaseOracle PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME reader_base_oracle
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_base_owner.py
    $<TARGET_FILE:ReaderBaseOracle> ${CMAKE_CURRENT_BINARY_DIR})

add_test(NAME reader_toolbar_resource
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_toolbar_resource.py)

foreach(target ReaderBaseOracle)
  target_compile_options(${target} PRIVATE -UNDEBUG)
endforeach()

add_test(NAME reader_toolbar_publication
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_toolbar_owner_publication.py
    --repo ${REPO_ROOT} --archive ${XP_FFI_HOST_ARCHIVE})

add_test(NAME reader_prepaint_input
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_prepaint_input.py)

add_test(NAME reader_base_publication
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_base_publication.py)

add_test(NAME reader_close_overlay
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_close_overlay.py)

add_test(NAME reader_overlay_input
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_overlay_input.py)

add_test(NAME reader_build_popup_lifetime
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_build_popup_lifetime.py)
add_test(NAME reader_child_metadata_faults
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_child_metadata_faults.py)
add_test(NAME reader_child_section_interleaving
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_child_section_interleaving.py)
add_test(NAME reader_child_native_faults
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_child_native_faults.py)
add_test(NAME reader_clipping_store_interleaving
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_clipping_store_interleaving.py
    --repo ${REPO_ROOT})
add_test(NAME reader_loop_link_faults
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_loop_link_faults.py)
add_test(NAME reader_font_picker_admission
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_font_picker_admission.py
    --repo ${REPO_ROOT})

add_test(NAME reader_app_owned_epub
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_app_owned_epub.py
    --repo ${REPO_ROOT} --archive ${XP_FFI_HOST_ARCHIVE})

add_test(NAME reader_toc_more_admission
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_toc_more_admission.py
    --repo ${REPO_ROOT})

add_test(NAME reader_idle_projection
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_idle_projection.py)

add_test(NAME reader_overlay_view
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_overlay_view.py)

add_test(NAME reader_overlay_open
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_overlay_open.py)

add_test(NAME reader_navigation
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_navigation.py)

add_test(NAME reader_book_render
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_book_render.py)

add_test(NAME reader_book_render_release
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_book_render.py --release-log)

add_test(NAME reader_recovery
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_recovery.py)

add_test(NAME reader_bookmark_parent
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_bookmark_parent.py)
add_test(NAME reader_bookmark_parent_release
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_bookmark_parent.py --release-log)

add_test(NAME reader_bookmark_section_drop
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_bookmark_parent.py --section-drop)

add_test(NAME reader_clipping_parent
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_clipping_parent.py)
add_test(NAME reader_clipping_parent_release
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_clipping_parent.py --release-log)

add_test(NAME reader_view_values
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_view_values.py)

add_test(NAME reader_book_lifetime
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_book_lifetime.py)

add_test(NAME reader_book_lifetime_release
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_book_lifetime.py --release-log)

add_test(NAME reader_build_policy
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_build_policy.py)

add_test(NAME reader_build_policy_release
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_build_policy.py --release-log)

add_test(NAME reader_settings_parent
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_reader_settings.py)

add_test(NAME reader_child_parent
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_child_parent.py)

add_test(NAME reader_child_parent_release
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_child_parent.py --release-log)

add_test(NAME reader_menu_parent
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_menu_parent.py)

add_test(NAME reader_menu_parent_release
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_menu_parent.py --release-log)

add_test(NAME reader_projection
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_projection.py)

add_test(NAME reader_failed_scene
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_failed_scene_owner.py
    --repo ${REPO_ROOT} --archive ${XP_FFI_HOST_ARCHIVE})

add_test(NAME reader_input_bridge
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_input_bridge.py)

add_test(NAME reader_render_completion
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_render_completion.py)

add_test(NAME reader_read_folder
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_read_folder.py)

add_test(NAME reader_overlay_helpers
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_helpers.py
    --reference ${CMAKE_CURRENT_SOURCE_DIR}/helper_reference --mode overlay)

add_test(NAME reader_refresh_orientation_helpers
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_helpers.py
    --reference ${CMAKE_CURRENT_SOURCE_DIR}/helper_reference --mode refresh-orientation)

add_test(NAME reader_link_hit_helper
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_link_text_helpers.py --mode link-hit)

add_test(NAME reader_summary_helper
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_link_text_helpers.py --mode sanitize)

add_test(NAME reader_position_helper
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_position_session.py)

add_test(NAME reader_popup_resource
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_popup_resource.py)

add_test(NAME reader_menu_confirm_admission
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_menu_confirm_admission.py
    --archive ${XP_FFI_HOST_ARCHIVE})

add_test(NAME reader_app_epub_a1
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_app_epub_a1.py
    --repo ${REPO_ROOT} --archive-off ${XP_FFI_HOST_ARCHIVE})

add_test(NAME reader_app_epub_results
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_app_results.py
    --repo ${REPO_ROOT} --archive ${XP_FFI_HOST_ARCHIVE} --mutants)

add_test(NAME reader_child_handle_composition
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_child_handle_composition.py
    --archive ${XP_FFI_HOST_ARCHIVE} --mutants)

add_test(NAME reader_stack_boundaries
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_stack_boundaries.py)

add_test(NAME reader_app_routing
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_app_routing.py
    --repo ${REPO_ROOT} --archive ${XP_FFI_HOST_ARCHIVE})

add_test(NAME reader_book_resources
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_book_resource.py)

add_test(NAME reader_book_cache
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_book_cache.py)

add_test(NAME reader_book_scratch
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_book_scratch.py)

add_test(NAME reader_book_zip
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_book_zip.py)

add_test(NAME reader_book_writer
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_book_writer.py)

add_test(NAME reader_book_package
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_book_package.py)

add_test(NAME reader_book_css
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_book_css.py)

add_test(NAME reader_book_open
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_book_open.py)

add_test(NAME reader_book_txt
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_book_txt.py)

add_test(NAME reader_book_items
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_book_items.py)

add_test(NAME reader_book_css_files
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_book_css_files.py)

add_test(NAME reader_book_load
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_book_load.py --success-only)
add_test(NAME reader_book_load_routes
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_book_load_routes.py)
add_test(NAME reader_book_load_oom
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_book_load_oom.py)

add_test(NAME reader_book_facade
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_book_facade.py)
add_test(NAME reader_book_section
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_book_section.py
    --archive ${XP_FFI_HOST_ARCHIVE})

add_test(NAME reader_book_text_consumers
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_book_text.py)

add_test(NAME reader_section_records
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_section_records.py)

add_test(NAME reader_section_storage
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_section_storage.py)

add_test(NAME reader_section_driver
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_section_driver.py)

add_test(NAME reader_section_render_spec
  COMMAND ${CMAKE_COMMAND} -E env "XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_render_spec.py)
