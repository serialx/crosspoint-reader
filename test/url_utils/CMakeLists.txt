add_executable(UrlUtilsTest
  UrlUtilsTest.cpp
  ${REPO_ROOT}/src/util/UrlUtils.cpp
  ${REPO_ROOT}/lib/HttpHeader/HttpText.cpp
)

target_include_directories(UrlUtilsTest PRIVATE
  ${REPO_ROOT}/src/util
  ${REPO_ROOT}/lib/HttpHeader
)

target_link_libraries(UrlUtilsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(UrlUtilsTest)
