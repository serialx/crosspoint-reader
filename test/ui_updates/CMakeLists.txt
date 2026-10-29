find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME ui_updates_original_effects
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/effects.py)
add_test(NAME ui_updates_original_widgets
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_widgets.py)
add_test(NAME ui_updates_original_percent
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_percent.py)

add_test(NAME ui_updates_abi COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_abi.py)
add_test(NAME ui_updates_service_parity COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/candidate_effects.py ${XP_FFI_HOST_ARCHIVE})
get_target_property(UPDATES_RENDER_SOURCES UiSceneTarget SOURCES)
list(REMOVE_ITEM UPDATES_RENDER_SOURCES UiSceneTarget.cpp)
list(TRANSFORM UPDATES_RENDER_SOURCES PREPEND "${REPO_ROOT}/test/text_golden/" REGEX "^[^/]")
foreach(target IN ITEMS UpdatesTarget FontRowsTarget)
  add_executable(${target} ${target}.cpp ${UPDATES_RENDER_SOURCES})
  target_include_directories(${target} PRIVATE ${REPO_ROOT}/test/text_golden $<TARGET_PROPERTY:UiSceneTarget,INCLUDE_DIRECTORIES>)
  target_compile_definitions(${target} PRIVATE $<TARGET_PROPERTY:UiSceneTarget,COMPILE_DEFINITIONS>)
  target_compile_options(${target} PRIVATE $<TARGET_PROPERTY:UiSceneTarget,COMPILE_OPTIONS> -UNDEBUG)
  target_link_options(${target} PRIVATE $<TARGET_PROPERTY:UiSceneTarget,LINK_OPTIONS>)
  target_link_libraries(${target} PRIVATE xp_ffi_host)
  add_test(NAME ui_updates_${target} COMMAND ${target})
endforeach()
add_test(NAME ui_updates_failure_bridge COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/test_failure_bridge.py ${XP_FFI_HOST_ARCHIVE})
