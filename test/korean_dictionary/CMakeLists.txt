add_executable(KoreanDictionaryTest
  KoreanDictionaryTest.cpp
  ${REPO_ROOT}/src/util/KoreanDictionary.cpp
)

target_include_directories(KoreanDictionaryTest PRIVATE ${REPO_ROOT}/src/util)

target_link_libraries(KoreanDictionaryTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(KoreanDictionaryTest)
