find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME ui_settings_cpp_goldens COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_oracle.py)

add_executable(settings_policy_replay PolicyReplay.cpp
  ${REPO_ROOT}/src/activities/settings/SettingsUiAbi.cpp)
target_link_libraries(settings_policy_replay PRIVATE crosspoint_test_common)
add_test(NAME ui_settings_rust_policy
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/replay.py $<TARGET_FILE:settings_policy_replay>)

get_target_property(SETTINGS_RENDER_SOURCES UiSceneTarget SOURCES)
list(REMOVE_ITEM SETTINGS_RENDER_SOURCES UiSceneTarget.cpp)
list(TRANSFORM SETTINGS_RENDER_SOURCES PREPEND "${REPO_ROOT}/test/text_golden/" REGEX "^[^/]")
add_executable(SettingsTarget SettingsTarget.cpp ${SETTINGS_RENDER_SOURCES})
target_include_directories(SettingsTarget PRIVATE ${REPO_ROOT}/test/text_golden $<TARGET_PROPERTY:UiSceneTarget,INCLUDE_DIRECTORIES>)
target_compile_definitions(SettingsTarget PRIVATE $<TARGET_PROPERTY:UiSceneTarget,COMPILE_DEFINITIONS>)
target_compile_options(SettingsTarget PRIVATE $<TARGET_PROPERTY:UiSceneTarget,COMPILE_OPTIONS> -UNDEBUG)
target_link_options(SettingsTarget PRIVATE $<TARGET_PROPERTY:UiSceneTarget,LINK_OPTIONS>)
target_link_libraries(SettingsTarget PRIVATE xp_ffi_host)
add_test(NAME ui_settings_pixels COMMAND SettingsTarget)

add_test(NAME ui_settings_ble_bridge COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/test_ble_bridge.py ${XP_FFI_HOST_ARCHIVE})
add_test(NAME ui_settings_protocol COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/test_protocol.py ${XP_FFI_HOST_ARCHIVE})

add_test(NAME ui_settings_arena_recovery COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/test_recovery.py)
