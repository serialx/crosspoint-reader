find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_path(BOOKMARK_TEST_ARDUINOJSON ArduinoJson.h
  HINTS ${REPO_ROOT}/.pio/libdeps/default/ArduinoJson/src)
if(BOOKMARK_TEST_ARDUINOJSON)
  add_test(NAME bookmark_bytes_oracle
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
      --build ${CMAKE_CURRENT_BINARY_DIR}/oracle
      --arduinojson ${BOOKMARK_TEST_ARDUINOJSON}
      --archive ${XP_FFI_HOST_ARCHIVE}
      --negative-controls)
else()
  message(STATUS "Bookmark byte oracle needs ArduinoJson 7.4.2 (BOOKMARK_TEST_ARDUINOJSON)")
endif()
