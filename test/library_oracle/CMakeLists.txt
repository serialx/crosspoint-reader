add_executable(LibraryOracle
  Oracle.cpp
  ${REPO_ROOT}/lib/LibraryIndex/LibraryBuilder.cpp
  ${REPO_ROOT}/lib/LibraryIndex/LibraryIndexFile.cpp
  ${REPO_ROOT}/lib/LibraryIndex/LibraryFormat.cpp
  ${REPO_ROOT}/lib/LibraryIndex/LibraryText.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(LibraryOracle PRIVATE
  ${REPO_ROOT}/test/library_builder/stubs
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/LibraryIndex
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Utf8
)
target_link_libraries(LibraryOracle PRIVATE crosspoint_test_common)
add_test(NAME LibraryOracleBytes COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/check.py $<TARGET_FILE:LibraryOracle>)

add_executable(LibraryRustAllocationTest
  RustAllocationTest.cpp
  ${REPO_ROOT}/lib/LibraryIndex/LibraryBuilder.cpp
  ${REPO_ROOT}/lib/LibraryIndex/LibraryIndexFile.cpp
  ${REPO_ROOT}/lib/LibraryIndex/LibraryFormat.cpp
  ${REPO_ROOT}/lib/LibraryIndex/LibraryText.cpp
)
target_include_directories(LibraryRustAllocationTest PRIVATE
  ${REPO_ROOT}/test/library_builder/stubs
  ${REPO_ROOT}/lib/LibraryIndex
)
target_compile_definitions(LibraryRustAllocationTest PRIVATE XP_FFI_HOST_FAULT_INJECTION)
target_link_libraries(LibraryRustAllocationTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(LibraryRustAllocationTest)

add_test(NAME LibraryRecoveryBytes COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/check.py $<TARGET_FILE:LibraryOracle> recovery.bin.gz --recovery)
