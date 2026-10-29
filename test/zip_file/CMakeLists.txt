add_executable(ZipFileTest
  ZipFileTest.cpp
  ${REPO_ROOT}/lib/ZipFile/ZipFile.cpp
  ${REPO_ROOT}/lib/miniz/src/InflateStream.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/lib/hal/HalMemory.cpp
)
target_include_directories(ZipFileTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/ZipFile
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/lib/miniz/src
)
target_link_libraries(ZipFileTest PRIVATE xp_ffi_host crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ZipFileTest)

# Inspect the existing private cursor/stat contract without changing the firmware API.
target_compile_options(ZipFileTest PRIVATE -fno-access-control)
target_compile_definitions(ZipFileTest PRIVATE
  ZIP_EPUB_DIR="${REPO_ROOT}/test/epubs"
  ZIP_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(ZipIoOracle IoOracle.cpp
  ${REPO_ROOT}/lib/ZipFile/ZipFile.cpp
  ${REPO_ROOT}/lib/miniz/src/InflateStream.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/lib/hal/HalMemory.cpp)
get_target_property(ZIP_ORACLE_INCLUDES ZipFileTest INCLUDE_DIRECTORIES)
target_include_directories(ZipIoOracle PRIVATE ${ZIP_ORACLE_INCLUDES})
target_link_libraries(ZipIoOracle PRIVATE xp_ffi_host crosspoint_test_common)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME ZipIoOracle COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/io_oracle.py $<TARGET_FILE:ZipIoOracle>)
