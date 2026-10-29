find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME ui_network_cpp_oracle COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_oracle.py)

add_test(NAME ui_network_usb_effects COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/test_usb_effects.py ${XP_FFI_HOST_ARCHIVE})
add_executable(NetworkUsbGeometry UsbGeometry.cpp
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp)
target_include_directories(NetworkUsbGeometry PRIVATE
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include)
target_compile_definitions(NetworkUsbGeometry PRIVATE FREEINK_UI_COMPONENT_STYLE=0)
target_compile_options(NetworkUsbGeometry PRIVATE -UNDEBUG)
target_link_libraries(NetworkUsbGeometry PRIVATE xp_ffi_host)
add_test(NAME ui_network_usb_geometry COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/test_usb_geometry.py $<TARGET_FILE:NetworkUsbGeometry>)

get_target_property(NETWORK_RENDER_SOURCES UiSceneTarget SOURCES)
list(REMOVE_ITEM NETWORK_RENDER_SOURCES UiSceneTarget.cpp)
list(TRANSFORM NETWORK_RENDER_SOURCES PREPEND "${REPO_ROOT}/test/text_golden/" REGEX "^[^/]")
add_executable(NetworkUsbTarget UsbTarget.cpp ${NETWORK_RENDER_SOURCES})
target_include_directories(NetworkUsbTarget PRIVATE ${REPO_ROOT}/test/text_golden $<TARGET_PROPERTY:UiSceneTarget,INCLUDE_DIRECTORIES>)
target_compile_definitions(NetworkUsbTarget PRIVATE $<TARGET_PROPERTY:UiSceneTarget,COMPILE_DEFINITIONS>)
target_compile_options(NetworkUsbTarget PRIVATE $<TARGET_PROPERTY:UiSceneTarget,COMPILE_OPTIONS> -UNDEBUG)
target_link_options(NetworkUsbTarget PRIVATE $<TARGET_PROPERTY:UiSceneTarget,LINK_OPTIONS>)
target_link_libraries(NetworkUsbTarget PRIVATE xp_ffi_host)
add_test(NAME ui_network_usb_pixels COMMAND NetworkUsbTarget)

add_test(NAME ui_network_modes_oracle COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_modes.py)
add_executable(NetworkModesGeometry ModesGeometry.cpp
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp)
target_include_directories(NetworkModesGeometry PRIVATE ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include)
target_compile_definitions(NetworkModesGeometry PRIVATE FREEINK_UI_COMPONENT_STYLE=0)
target_compile_options(NetworkModesGeometry PRIVATE -UNDEBUG)
target_link_libraries(NetworkModesGeometry PRIVATE xp_ffi_host)
add_test(NAME ui_network_modes_geometry COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/test_modes_replay.py $<TARGET_FILE:NetworkModesGeometry>)
add_test(NAME ui_network_modes_input COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/test_modes_input.py ${XP_FFI_HOST_ARCHIVE})
add_executable(NetworkModesTarget ModesTarget.cpp ${NETWORK_RENDER_SOURCES})
target_include_directories(NetworkModesTarget PRIVATE ${REPO_ROOT}/test/text_golden $<TARGET_PROPERTY:UiSceneTarget,INCLUDE_DIRECTORIES>)
target_compile_definitions(NetworkModesTarget PRIVATE $<TARGET_PROPERTY:UiSceneTarget,COMPILE_DEFINITIONS>)
target_compile_options(NetworkModesTarget PRIVATE $<TARGET_PROPERTY:UiSceneTarget,COMPILE_OPTIONS> -UNDEBUG)
target_link_options(NetworkModesTarget PRIVATE $<TARGET_PROPERTY:UiSceneTarget,LINK_OPTIONS>)
target_link_libraries(NetworkModesTarget PRIVATE xp_ffi_host)
add_test(NAME ui_network_modes_pixels COMMAND NetworkModesTarget)

add_test(NAME ui_network_usb_input COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/test_usb_input.py ${XP_FFI_HOST_ARCHIVE})

add_test(NAME ui_network_abi COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_abi.py)
