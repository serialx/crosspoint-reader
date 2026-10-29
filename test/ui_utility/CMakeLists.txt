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

target_compile_definitions(UiUtilityPixels PRIVATE UI_UTILITY_CANDIDATE=1)
add_test(NAME ui_utility_rust_pixels COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/pixels.py $<TARGET_FILE:UiUtilityPixels> rust)
set(UTILITY_PARENT_COPY ${CMAKE_CURRENT_BINARY_DIR}/parents)
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/prepare_parents.py ${UTILITY_PARENT_COPY}
  COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${REPO_ROOT}/src/activities/util/UtilityActivity.cpp
  ${REPO_ROOT}/src/activities/util/IntervalSelectionActivity.cpp
  ${REPO_ROOT}/src/activities/ActivityManager.cpp
  ${REPO_ROOT}/src/MappedInputManager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/parents.cpp ${CMAKE_CURRENT_SOURCE_DIR}/prepare_parents.py)
add_executable(UiUtilityParents ${UTILITY_PARENT_COPY}/parents.cpp ${UTILITY_PARENT_COPY}/RustActivityAdapter.cpp)
target_include_directories(UiUtilityParents PRIVATE ${UTILITY_PARENT_COPY})
target_link_libraries(UiUtilityParents PRIVATE xp_ffi_host)
target_compile_options(UiUtilityParents PRIVATE -UNDEBUG)
add_test(NAME ui_utility_parents COMMAND UiUtilityParents)
