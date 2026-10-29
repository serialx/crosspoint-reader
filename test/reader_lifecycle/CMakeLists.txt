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
