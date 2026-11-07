find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME web_server_oracle
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract.py
    --build ${CMAKE_CURRENT_BINARY_DIR}/oracle
    --arduinojson ${XP_TEST_ARDUINOJSON_SRC}
    --html-root ${XP_TEST_WEB_DIR}
    --archive ${XP_FFI_HOST_ARCHIVE}
    --mutation-check --review-check)

add_test(NAME web_discovery_budget
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/discovery_budget.py
    --build ${CMAKE_CURRENT_BINARY_DIR}/discovery-budget)
