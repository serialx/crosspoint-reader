find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME ui_utility_cpp_goldens COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_oracle.py)
get_target_property(UTILITY_RENDERER_SOURCES TextGolden SOURCES)
list(FILTER UTILITY_RENDERER_SOURCES EXCLUDE REGEX "TextGolden.cpp$")
set_source_files_properties(${CMAKE_BINARY_DIR}/text_golden/instrumented/GfxRenderer.cpp
  ${CMAKE_BINARY_DIR}/text_golden/instrumented/RustUiRenderer.cpp PROPERTIES GENERATED TRUE)
add_executable(UiUtilityPixels pixels.cpp ${UTILITY_RENDERER_SOURCES}
  ${CMAKE_BINARY_DIR}/text_golden/instrumented/RustUiRenderer.cpp
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp)
target_include_directories(UiUtilityPixels PRIVATE $<TARGET_PROPERTY:TextGolden,INCLUDE_DIRECTORIES>
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include ${REPO_ROOT}/test/text_golden)
target_compile_definitions(UiUtilityPixels PRIVATE $<TARGET_PROPERTY:TextGolden,COMPILE_DEFINITIONS>)
target_compile_options(UiUtilityPixels PRIVATE $<TARGET_PROPERTY:TextGolden,COMPILE_OPTIONS> -UNDEBUG)
target_link_options(UiUtilityPixels PRIVATE $<TARGET_PROPERTY:TextGolden,LINK_OPTIONS>)
target_link_libraries(UiUtilityPixels PRIVATE xp_ffi_host)

add_dependencies(UiUtilityPixels TextGolden)
add_test(NAME ui_utility_cpp_pixels COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/pixels.py $<TARGET_FILE:UiUtilityPixels> cpp)
