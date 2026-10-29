find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME plugin_policy_oracle COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
  --build ${CMAKE_CURRENT_BINARY_DIR}/oracle --archive ${XP_FFI_HOST_ARCHIVE})
add_test(NAME plugin_policy_json_faults COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
  --build ${CMAKE_CURRENT_BINARY_DIR}/json-faults --archive ${XP_FFI_HOST_ARCHIVE} --json-faults)

add_test(NAME plugin_policy_token_lifetime COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
  --build ${CMAKE_CURRENT_BINARY_DIR}/token-lifetime --archive ${XP_FFI_HOST_ARCHIVE} --token-lifetime)

add_test(NAME plugin_policy_contract COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
  --build ${CMAKE_CURRENT_BINARY_DIR}/contract --archive ${XP_FFI_HOST_ARCHIVE} --contract)
