add_executable(FontCacheManagerTest
  FontCacheManagerTest.cpp
  ${REPO_ROOT}/test/font_cache/legacy/FontDecompressor.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(FontCacheManagerTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/test/text_golden/stubs
  ${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont/include
)

target_link_libraries(FontCacheManagerTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FontCacheManagerTest)
