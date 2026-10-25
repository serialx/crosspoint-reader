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
