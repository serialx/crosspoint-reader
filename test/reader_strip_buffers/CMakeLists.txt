add_executable(ReaderStripBuffersTest ReaderStripBuffersTest.cpp)
target_include_directories(ReaderStripBuffersTest PRIVATE
  ${REPO_ROOT}/src/activities/reader ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/rust/crates/xp-ffi/include)
target_compile_definitions(ReaderStripBuffersTest PRIVATE XP_STRIP_RENDER=1)
target_link_libraries(ReaderStripBuffersTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ReaderStripBuffersTest)
