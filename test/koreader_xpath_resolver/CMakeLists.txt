add_executable(KOReaderXPathResolverTest
  KOReaderXPathResolverTest.cpp
  ${REPO_ROOT}/lib/KOReaderSync/ChapterXPathResolver.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(KOReaderXPathResolverTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/KOReaderSync
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/lib/expat
)

target_compile_definitions(KOReaderXPathResolverTest PRIVATE
  XML_CONTEXT_BYTES=0
  XML_GE=0
)

target_link_libraries(KOReaderXPathResolverTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(KOReaderXPathResolverTest)

add_executable(XPathOracle
  XPathOracle.cpp
  ${REPO_ROOT}/lib/KOReaderSync/ChapterXPathResolver.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)
get_target_property(XPATH_INCLUDES KOReaderXPathResolverTest INCLUDE_DIRECTORIES)
target_include_directories(XPathOracle PRIVATE ${XPATH_INCLUDES})
target_compile_definitions(XPathOracle PRIVATE XML_CONTEXT_BYTES=0 XML_GE=0)
target_link_libraries(XPathOracle PRIVATE crosspoint_test_common)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME XPathGoldens COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/oracle.py --oracle $<TARGET_FILE:XPathOracle> verify)
