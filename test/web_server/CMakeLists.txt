find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_path(WEB_TEST_ARDUINOJSON ArduinoJson.h
  HINTS ${REPO_ROOT}/.pio/libdeps/default/ArduinoJson/src)
if(WEB_TEST_ARDUINOJSON)
  add_test(NAME web_server_oracle
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract.py
      --build ${CMAKE_CURRENT_BINARY_DIR}/oracle
      --arduinojson ${WEB_TEST_ARDUINOJSON}
      --archive ${XP_FFI_HOST_ARCHIVE}
      --mutation-check --review-check)
else()
  message(STATUS "Web server oracle needs WEB_TEST_ARDUINOJSON")
endif()

add_test(NAME web_discovery_budget
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/discovery_budget.py
    --build ${CMAKE_CURRENT_BINARY_DIR}/discovery-budget)
