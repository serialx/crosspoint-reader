find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(READER_METHODS ${CMAKE_CURRENT_BINARY_DIR}/reader_methods.inc)
add_custom_command(OUTPUT ${READER_METHODS}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract.py ${REPO_ROOT} ${READER_METHODS}
  DEPENDS extract.py ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp
    ${REPO_ROOT}/src/activities/reader/ReaderUtils.h
  VERBATIM)
add_executable(ReaderLifecycleOracle oracle.cpp ${READER_METHODS}
  ${REPO_ROOT}/src/activities/reader/ReaderSession.cpp)
target_include_directories(ReaderLifecycleOracle PRIVATE stubs ${CMAKE_CURRENT_BINARY_DIR}
  ${REPO_ROOT}/src/activities/reader ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/BlePageTurner/include
  ${REPO_ROOT}/freeink-sdk/libs/network/BleKeyboardHost/include)
target_link_libraries(ReaderLifecycleOracle PRIVATE crosspoint_test_common)
add_test(NAME reader_lifecycle_oracle
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/compare.py
    $<TARGET_FILE:ReaderLifecycleOracle> ${CMAKE_CURRENT_SOURCE_DIR}/lifecycle.golden.gz)

set(RENDER_METHODS ${CMAKE_CURRENT_BINARY_DIR}/render_methods.inc)
add_custom_command(OUTPUT ${RENDER_METHODS}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract_render.py ${REPO_ROOT} ${RENDER_METHODS}
  DEPENDS extract_render.py extract.py ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp
    ${REPO_ROOT}/src/activities/reader/ReaderUtils.h
  VERBATIM)
foreach(mode off on)
  add_executable(ReaderRenderOracle_${mode} render_oracle.cpp ${RENDER_METHODS})
  target_link_libraries(ReaderRenderOracle_${mode} PRIVATE crosspoint_test_common)
  target_include_directories(ReaderRenderOracle_${mode} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  target_compile_options(ReaderRenderOracle_${mode} PRIVATE -Wno-unused-parameter -Wno-missing-field-initializers)
  if(mode STREQUAL "on")
    target_compile_definitions(ReaderRenderOracle_${mode} PRIVATE XP_STRIP_RENDER=1)
  endif()
  add_test(NAME reader_render_oracle_${mode}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/compare.py
      $<TARGET_FILE:ReaderRenderOracle_${mode}> ${CMAKE_CURRENT_SOURCE_DIR}/render-${mode}.golden.gz)
endforeach()

add_executable(ReaderLifecycleAbi abi.cpp)
target_link_libraries(ReaderLifecycleAbi PRIVATE crosspoint_test_common)
target_compile_options(ReaderLifecycleAbi PRIVATE -UNDEBUG)
add_test(NAME reader_lifecycle_abi COMMAND ReaderLifecycleAbi)

set(INPUT_METHODS ${CMAKE_CURRENT_BINARY_DIR}/input_methods.inc)
add_custom_command(OUTPUT ${INPUT_METHODS}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract_input.py ${REPO_ROOT} ${INPUT_METHODS}
  DEPENDS extract_input.py extract.py ${REPO_ROOT}/src/activities/reader/ReaderUtils.h
  VERBATIM)
add_executable(ReaderInputOracle input_oracle.cpp ${INPUT_METHODS})
target_include_directories(ReaderInputOracle PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${REPO_ROOT}/src/activities/reader)
target_link_libraries(ReaderInputOracle PRIVATE crosspoint_test_common)
add_test(NAME reader_input_oracle
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/compare.py
    $<TARGET_FILE:ReaderInputOracle> ${CMAKE_CURRENT_SOURCE_DIR}/input.golden.gz)

add_executable(ReaderNavOracle nav_oracle.cpp)
target_compile_definitions(ReaderNavOracle PRIVATE READER_NAV_RUST=1)
target_link_libraries(ReaderNavOracle PRIVATE crosspoint_test_common)
target_include_directories(ReaderNavOracle PRIVATE ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include)
add_test(NAME reader_nav_oracle
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/compare.py
    $<TARGET_FILE:ReaderNavOracle> ${CMAKE_CURRENT_SOURCE_DIR}/nav.golden.gz)

set(LOOP_METHODS ${CMAKE_CURRENT_BINARY_DIR}/loop_methods.inc)
add_custom_command(OUTPUT ${LOOP_METHODS}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract_loop.py ${REPO_ROOT} ${LOOP_METHODS}
  DEPENDS extract_loop.py extract.py ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp
    ${REPO_ROOT}/src/activities/reader/ReaderUtils.h
  VERBATIM)
add_executable(ReaderLoopOracle loop_oracle.cpp ${LOOP_METHODS})
target_include_directories(ReaderLoopOracle PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${REPO_ROOT}/src)
add_test(NAME reader_loop_oracle
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/compare.py
    $<TARGET_FILE:ReaderLoopOracle> ${CMAKE_CURRENT_SOURCE_DIR}/loop.golden.gz)

add_executable(ReaderLoopOracleRelease loop_oracle.cpp ${LOOP_METHODS})
target_compile_definitions(ReaderLoopOracleRelease PRIVATE LOOP_LOG_LEVEL=1)
target_include_directories(ReaderLoopOracleRelease PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${REPO_ROOT}/src)
add_test(NAME reader_loop_oracle_release
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/compare.py
    $<TARGET_FILE:ReaderLoopOracleRelease> ${CMAKE_CURRENT_SOURCE_DIR}/loop-release.golden.gz)
