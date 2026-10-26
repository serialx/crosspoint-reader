add_executable(ContentOpfParserTest
  ContentOpfParserTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/PackageParser.cpp
  EncryptionManifestProbeTest.cpp
)

target_include_directories(ContentOpfParserTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/Epub/Epub/parsers
  ${REPO_ROOT}/lib/XmlParserUtils
)

target_link_libraries(ContentOpfParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ContentOpfParserTest)

include(${REPO_ROOT}/test/cmake/xp_ffi_host.cmake)
target_link_libraries(ContentOpfParserTest PRIVATE xp_ffi_host)
