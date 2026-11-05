add_executable(ChapterPositionTest
  ChapterPositionTest.cpp
)

target_link_libraries(ChapterPositionTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

target_compile_options(ChapterPositionTest PRIVATE -UNDEBUG)

gtest_discover_tests(ChapterPositionTest)
