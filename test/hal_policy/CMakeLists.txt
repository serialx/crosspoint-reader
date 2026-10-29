add_test(NAME hal_policy_oracle COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
  --archive ${XP_FFI_HOST_ARCHIVE})
add_executable(HalPolicyAbi abi.cpp)
target_link_libraries(HalPolicyAbi PRIVATE xp_ffi_host)
target_compile_options(HalPolicyAbi PRIVATE -UNDEBUG)
add_test(NAME hal_policy_abi COMMAND HalPolicyAbi)
