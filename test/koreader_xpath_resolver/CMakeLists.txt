add_executable(KOReaderXPathResolverTest
  KOReaderXPathResolverTest.cpp
  XPathParserFailureTest.cpp
  ${REPO_ROOT}/lib/KOReaderSync/ChapterXPathResolver.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(KOReaderXPathResolverTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/KOReaderSync
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Utf8
)

include(${REPO_ROOT}/test/cmake/xp_ffi_host.cmake)

target_link_libraries(KOReaderXPathResolverTest PRIVATE
  crosspoint_test_common
  xp_ffi_host
  GTest::gtest_main
)

target_compile_definitions(KOReaderXPathResolverTest PRIVATE XP_FFI_HOST_FAULT_INJECTION)

gtest_discover_tests(KOReaderXPathResolverTest)

add_executable(XPathOracle
  XPathOracle.cpp
  ${REPO_ROOT}/lib/KOReaderSync/ChapterXPathResolver.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
get_target_property(XPATH_INCLUDES KOReaderXPathResolverTest INCLUDE_DIRECTORIES)
target_include_directories(XPathOracle PRIVATE ${XPATH_INCLUDES})
target_link_libraries(XPathOracle PRIVATE crosspoint_test_common xp_ffi_host)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME XPathGoldens COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/oracle.py --oracle $<TARGET_FILE:XPathOracle> verify)
