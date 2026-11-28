find_package(ZLIB REQUIRED)

add_executable(InflateReaderTest InflateReaderTest.cpp
  ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp
  ${REPO_ROOT}/src/util/DictZip.cpp)
target_include_directories(InflateReaderTest PRIVATE
  ../inflate_small/stubs
  ${REPO_ROOT}/lib/InflateReader
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/src/util)
target_link_libraries(InflateReaderTest PRIVATE xp_ffi_host crosspoint_test_common GTest::gtest_main ZLIB::ZLIB)
gtest_discover_tests(InflateReaderTest)
