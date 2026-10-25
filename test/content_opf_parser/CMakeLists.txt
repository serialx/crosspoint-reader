add_executable(ContentOpfParserTest
  ContentOpfParserTest.cpp
  EncryptionManifestProbeTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ContentOpfParser.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(ContentOpfParserTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/Epub/Epub/parsers
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/lib/expat
)

target_compile_definitions(ContentOpfParserTest PRIVATE XML_GE=0 XML_CONTEXT_BYTES=0)

target_link_libraries(ContentOpfParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ContentOpfParserTest)

include(${REPO_ROOT}/test/cmake/xp_ffi_host.cmake)
target_link_libraries(ContentOpfParserTest PRIVATE xp_ffi_host)
