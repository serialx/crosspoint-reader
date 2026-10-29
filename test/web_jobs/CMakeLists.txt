find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME web_jobs_oracle COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
  --build ${CMAKE_CURRENT_BINARY_DIR}/oracle --archive ${XP_FFI_HOST_ARCHIVE})

add_test(NAME web_dav_oracle COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
  --build ${CMAKE_CURRENT_BINARY_DIR}/dav --archive ${XP_FFI_HOST_ARCHIVE} --dav)
add_test(NAME web_lcp_oracle COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
  --build ${CMAKE_CURRENT_BINARY_DIR}/lcp --archive ${XP_FFI_HOST_ARCHIVE} --lcp)

add_test(NAME web_lcp_disabled_oracle COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
  --build ${CMAKE_CURRENT_BINARY_DIR}/lcp-disabled --archive ${XP_FFI_HOST_ARCHIVE} --lcp --no-enroll-key)

add_test(NAME web_dav_allocation COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
  --build ${CMAKE_CURRENT_BINARY_DIR}/dav-allocation --archive ${XP_FFI_HOST_ARCHIVE} --dav-allocation)
add_test(NAME shared_system_item_policy COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
  --build ${CMAKE_CURRENT_BINARY_DIR}/system-policy --archive ${XP_FFI_HOST_ARCHIVE} --system-policy)
