find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_path(STORE_TEST_ARDUINOJSON ArduinoJson.h
  HINTS ${REPO_ROOT}/.pio/libdeps/default/ArduinoJson/src)
find_path(STORE_TEST_MBEDTLS library/base64.c
  HINTS $ENV{HOME}/.platformio/packages/framework-espidf/components/mbedtls/mbedtls)
if(STORE_TEST_ARDUINOJSON AND STORE_TEST_MBEDTLS)
  add_test(NAME settings_store_oracle
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
      --build ${CMAKE_CURRENT_BINARY_DIR}/oracle
      --arduinojson ${STORE_TEST_ARDUINOJSON}
      --mbedtls ${STORE_TEST_MBEDTLS}
      --archive ${XP_FFI_HOST_ARCHIVE})
else()
  message(STATUS "Settings store oracle needs ArduinoJson and mbedTLS source paths (STORE_TEST_ARDUINOJSON, STORE_TEST_MBEDTLS)")
endif()
