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

add_test(NAME ui_network_calibre_effects COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/test_calibre_effects.py ${XP_FFI_HOST_ARCHIVE})

set(CALIBRE_TARGET ${CMAKE_CURRENT_BINARY_DIR}/CalibreTarget.cpp)
add_custom_command(OUTPUT ${CALIBRE_TARGET}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/prepare_calibre_target.py ${CALIBRE_TARGET}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/CalibreTarget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/prepare_calibre_target.py ${CMAKE_CURRENT_SOURCE_DIR}/calibre_render_original.inc
    ${REPO_ROOT}/src/activities/network/CalibreConnectActivity.cpp
    ${REPO_ROOT}/src/activities/network/NetworkUiBinding.h ${REPO_ROOT}/src/activities/network/NetworkUiBinding.cpp)
add_executable(NetworkCalibreTarget ${CALIBRE_TARGET} ${NETWORK_RENDER_SOURCES})
target_include_directories(NetworkCalibreTarget PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${REPO_ROOT}/test/text_golden $<TARGET_PROPERTY:UiSceneTarget,INCLUDE_DIRECTORIES>)
target_compile_definitions(NetworkCalibreTarget PRIVATE $<TARGET_PROPERTY:UiSceneTarget,COMPILE_DEFINITIONS>)
target_compile_options(NetworkCalibreTarget PRIVATE $<TARGET_PROPERTY:UiSceneTarget,COMPILE_OPTIONS> -UNDEBUG)
target_link_options(NetworkCalibreTarget PRIVATE $<TARGET_PROPERTY:UiSceneTarget,LINK_OPTIONS>)
target_link_libraries(NetworkCalibreTarget PRIVATE xp_ffi_host)
add_test(NAME ui_network_calibre_pixels COMMAND NetworkCalibreTarget)

add_test(NAME ui_network_calibre_input COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/test_calibre_input.py ${XP_FFI_HOST_ARCHIVE})

add_test(NAME ui_network_web_effects COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/test_web_effects.py ${XP_FFI_HOST_ARCHIVE})
set(WEB_TARGET ${CMAKE_CURRENT_BINARY_DIR}/WebTarget.cpp)
add_custom_command(OUTPUT ${WEB_TARGET}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/prepare_web_target.py ${WEB_TARGET}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/WebTarget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/prepare_web_target.py ${CMAKE_CURRENT_SOURCE_DIR}/web_render_original.inc
    ${REPO_ROOT}/src/util/QrUtils.cpp ${REPO_ROOT}/src/activities/network/CrossPointWebServerActivity.cpp
    ${REPO_ROOT}/src/activities/network/NetworkUiBinding.h ${REPO_ROOT}/src/activities/network/NetworkUiBinding.cpp)
add_executable(NetworkWebTarget ${WEB_TARGET} ${NETWORK_RENDER_SOURCES}
  ${REPO_ROOT}/.pio/libdeps/default/QRCode/src/qrcode.c)
target_include_directories(NetworkWebTarget PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/.pio/libdeps/default/QRCode/src
  ${REPO_ROOT}/test/text_golden $<TARGET_PROPERTY:UiSceneTarget,INCLUDE_DIRECTORIES>)
target_compile_definitions(NetworkWebTarget PRIVATE $<TARGET_PROPERTY:UiSceneTarget,COMPILE_DEFINITIONS>)
target_compile_options(NetworkWebTarget PRIVATE $<TARGET_PROPERTY:UiSceneTarget,COMPILE_OPTIONS> -UNDEBUG)
target_link_options(NetworkWebTarget PRIVATE $<TARGET_PROPERTY:UiSceneTarget,LINK_OPTIONS>)
target_link_libraries(NetworkWebTarget PRIVATE xp_ffi_host)
add_test(NAME ui_network_web_pixels COMMAND NetworkWebTarget)

add_test(NAME ui_network_web_input COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/test_web_input.py ${XP_FFI_HOST_ARCHIVE})
add_test(NAME ui_network_web_policy COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_web_policy.py)

add_test(NAME ui_network_wifi_entry_oracle COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/test_wifi_extra.py)

set(WIFI_TARGET ${CMAKE_CURRENT_BINARY_DIR}/WifiTarget.cpp)
add_custom_command(OUTPUT ${WIFI_TARGET}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/prepare_wifi_target.py ${WIFI_TARGET}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/WifiTarget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/prepare_wifi_target.py ${CMAKE_CURRENT_SOURCE_DIR}/wifi_render_original.inc)
add_executable(NetworkWifiTarget ${WIFI_TARGET} ${NETWORK_RENDER_SOURCES})
target_include_directories(NetworkWifiTarget PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${REPO_ROOT}/test/text_golden $<TARGET_PROPERTY:UiSceneTarget,INCLUDE_DIRECTORIES>)
target_compile_definitions(NetworkWifiTarget PRIVATE $<TARGET_PROPERTY:UiSceneTarget,COMPILE_DEFINITIONS>)
target_compile_options(NetworkWifiTarget PRIVATE $<TARGET_PROPERTY:UiSceneTarget,COMPILE_OPTIONS> -UNDEBUG)
target_link_options(NetworkWifiTarget PRIVATE $<TARGET_PROPERTY:UiSceneTarget,LINK_OPTIONS>)
target_link_libraries(NetworkWifiTarget PRIVATE xp_ffi_host)
add_test(NAME ui_network_wifi_pixels COMMAND NetworkWifiTarget)
