find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME web_server_oracle_catalog_off
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract.py
    --build ${CMAKE_CURRENT_BINARY_DIR}/oracle
    --arduinojson ${XP_TEST_ARDUINOJSON_SRC}
    --html-root ${XP_TEST_WEB_DIR}
    --catalog-off
    --mutation-check --review-check)

add_test(NAME web_discovery_budget
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/discovery_budget.py
    --build ${CMAKE_CURRENT_BINARY_DIR}/discovery-budget)

add_test(NAME web_server_catalog_shipping
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/catalog_shipping.py
    --build ${CMAKE_CURRENT_BINARY_DIR}/catalog-shipping
    --arduinojson ${XP_TEST_ARDUINOJSON_SRC}
    --html-root ${XP_TEST_WEB_DIR}
    --archive ${XP_FFI_HOST_ARCHIVE})

add_test(NAME plugin_http_connect_before_headers
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/plugin_connect.py
    --build ${CMAKE_CURRENT_BINARY_DIR}/plugin-connect)

add_test(NAME web_sdk_preconnected_retry
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/sdk_fetch_retry_contract.py
    --build ${CMAKE_CURRENT_BINARY_DIR}/sdk-fetch-retry
    --arduinojson ${XP_TEST_ARDUINOJSON_SRC}
    --html-root ${XP_TEST_WEB_DIR})

add_test(NAME web_asset_loading COMMAND node ${CMAKE_CURRENT_SOURCE_DIR}/web_assets.mjs)

if(XP_TEST_REQUIRE_ESP_COMPILERS)
  add_test(NAME web_parser_link_guard COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/parser_link_guard.py)
endif()
