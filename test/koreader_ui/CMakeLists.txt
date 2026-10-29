find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME koreader_ui_original_effects COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_effects.py)
add_test(NAME koreader_ui_original_views COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_views.py)

add_test(NAME koreader_ui_effects COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/candidate_effects.py ${XP_FFI_HOST_ARCHIVE})
set(KOREADER_PIXELS ${CMAKE_CURRENT_BINARY_DIR}/Pixels.cpp)
add_custom_command(OUTPUT ${KOREADER_PIXELS}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/make_pixels.py ${KOREADER_PIXELS}
  DEPENDS make_pixels.py pixels_main.inc view.cpp original_view.inc
  VERBATIM)
get_target_property(KOREADER_RENDER_SOURCES UiSceneTarget SOURCES)
list(REMOVE_ITEM KOREADER_RENDER_SOURCES UiSceneTarget.cpp)
list(TRANSFORM KOREADER_RENDER_SOURCES PREPEND "${REPO_ROOT}/test/text_golden/" REGEX "^[^/]")
add_executable(KOReaderPixels ${KOREADER_PIXELS} ${KOREADER_RENDER_SOURCES})
target_include_directories(KOReaderPixels PRIVATE ${REPO_ROOT}/test/text_golden
  ${REPO_ROOT}/src ${REPO_ROOT}/freeink-sdk/libs/assets/Icons/include
  $<TARGET_PROPERTY:UiSceneTarget,INCLUDE_DIRECTORIES>)
target_compile_definitions(KOReaderPixels PRIVATE $<TARGET_PROPERTY:UiSceneTarget,COMPILE_DEFINITIONS>)
target_compile_options(KOReaderPixels PRIVATE $<TARGET_PROPERTY:UiSceneTarget,COMPILE_OPTIONS> -UNDEBUG)
target_link_options(KOReaderPixels PRIVATE $<TARGET_PROPERTY:UiSceneTarget,LINK_OPTIONS>)
target_link_libraries(KOReaderPixels PRIVATE xp_ffi_host)
add_test(NAME koreader_ui_pixels COMMAND KOReaderPixels)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/adapter-methods.inc
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/adapter_methods.py
    ${REPO_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/adapter-methods.inc
  DEPENDS adapter_methods.py ${REPO_ROOT}/src/activities/reader/KOReaderUiActivity.cpp VERBATIM)
add_executable(koreader_panel_busy panel_busy.cpp ${CMAKE_CURRENT_BINARY_DIR}/adapter-methods.inc
  ${CMAKE_CURRENT_BINARY_DIR}/../ui_runtime/adapter/RustActivityAdapter.cpp)
target_include_directories(koreader_panel_busy PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/../ui_runtime/adapter ${REPO_ROOT}/test/ui_runtime/stubs)
target_compile_options(koreader_panel_busy PRIVATE -UNDEBUG)
target_link_libraries(koreader_panel_busy PRIVATE xp_ffi_host)
add_test(NAME koreader_panel_busy COMMAND koreader_panel_busy)
add_executable(koreader_float_order float_order.cpp)
target_link_libraries(koreader_float_order PRIVATE xp_ffi_host)
target_compile_options(koreader_float_order PRIVATE -UNDEBUG)
add_test(NAME koreader_float_order COMMAND koreader_float_order)
add_executable(koreader_touch touch.cpp ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp)
target_include_directories(koreader_touch PRIVATE ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include)
target_compile_options(koreader_touch PRIVATE -UNDEBUG)
target_link_libraries(koreader_touch PRIVATE xp_ffi_host)
add_test(NAME koreader_touch COMMAND koreader_touch)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/book-methods.inc
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/book_methods.py
    ${REPO_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/book-methods.inc
  DEPENDS book_methods.py ${REPO_ROOT}/src/activities/reader/KOReaderSyncActivity.cpp VERBATIM)
add_executable(koreader_book_lifetime book_lifetime.cpp ${CMAKE_CURRENT_BINARY_DIR}/book-methods.inc)
target_include_directories(koreader_book_lifetime PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(koreader_book_lifetime PRIVATE xp_ffi_host)
target_compile_options(koreader_book_lifetime PRIVATE -UNDEBUG)
add_test(NAME koreader_book_lifetime COMMAND koreader_book_lifetime)

add_test(NAME koreader_popup_recovery COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/popup_recovery.py)
