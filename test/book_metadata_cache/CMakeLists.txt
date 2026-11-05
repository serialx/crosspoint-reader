add_executable(BookMetadataCacheTest
  BookMetadataCacheTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/PackagePlatform.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(BookMetadataCacheTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
)

target_link_libraries(BookMetadataCacheTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookMetadataCacheTest)

target_sources(BookMetadataCacheTest PRIVATE ${REPO_ROOT}/lib/expat/ExpatErrors.cpp)
target_include_directories(BookMetadataCacheTest PRIVATE ${REPO_ROOT}/lib/expat ${REPO_ROOT}/test/content_opf_parser/stubs)
