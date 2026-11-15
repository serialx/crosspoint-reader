find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_path(STORE_TEST_MBEDTLS library/base64.c
  HINTS $ENV{HOME}/.platformio/packages/framework-espidf/components/mbedtls/mbedtls)
if(STORE_TEST_MBEDTLS)
  add_test(NAME settings_store_oracle
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
      --build ${CMAKE_CURRENT_BINARY_DIR}/oracle
      --arduinojson ${XP_TEST_ARDUINOJSON_SRC}
      --mbedtls ${STORE_TEST_MBEDTLS}
      --archive ${XP_FFI_HOST_ARCHIVE})
else()
  message(STATUS "Settings store oracle needs mbedTLS sources, set STORE_TEST_MBEDTLS to a mbedtls checkout")
endif()
