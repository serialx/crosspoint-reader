set(SDK_NETWORK ${REPO_ROOT}/freeink-sdk/libs/network)
set(COMPAT_CONSUMERS
  ConsumerOracle.cpp
  ${SDK_NETWORK}/Opds/src/OpdsFeedParser.cpp
  ${SDK_NETWORK}/Opds/src/OpdsParser.cpp
  ${SDK_NETWORK}/Opds/src/OpenSearchDescParser.cpp
  ${SDK_NETWORK}/Opds/src/Opds2Parser.cpp
  ${SDK_NETWORK}/JsonSax/src/StreamingJsonParser.cpp
  ${SDK_NETWORK}/CatalogList/src/XmlListParser.cpp
)
add_executable(ExpatConsumerBaseline ${COMPAT_CONSUMERS}
  ${REPO_ROOT}/test/vendor/expat/xmlparse.c
  ${REPO_ROOT}/test/vendor/expat/xmlrole.c
  ${REPO_ROOT}/test/vendor/expat/xmltok.c)
target_include_directories(ExpatConsumerBaseline PRIVATE
  stubs ${SDK_NETWORK}/Opds/include ${SDK_NETWORK}/CatalogList/include
  ${SDK_NETWORK}/JsonSax/include ${REPO_ROOT}/test/vendor/expat)
target_compile_definitions(ExpatConsumerBaseline PRIVATE XML_GE=0 XML_CONTEXT_BYTES=0)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME ExpatSdkBaseline COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/oracle.py
  --binary $<TARGET_FILE:ExpatConsumerBaseline>)

include(${REPO_ROOT}/test/cmake/xp_ffi_host.cmake)
add_executable(ExpatConsumerAdapter ${COMPAT_CONSUMERS}
  ${REPO_ROOT}/lib/expat/ExpatCompat.cpp
  ${REPO_ROOT}/lib/expat/ExpatErrors.cpp)
target_include_directories(ExpatConsumerAdapter PRIVATE
  stubs ${SDK_NETWORK}/Opds/include ${SDK_NETWORK}/CatalogList/include
  ${SDK_NETWORK}/JsonSax/include ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/test/koreader_xpath_resolver/stubs)
target_link_libraries(ExpatConsumerAdapter PRIVATE xp_ffi_host)
add_test(NAME ExpatSdkAdapter COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/oracle.py
  --binary $<TARGET_FILE:ExpatConsumerAdapter>)
add_executable(ExpatSdkCatalogTest
  ${SDK_NETWORK}/CatalogList/test/host/test_xml_list_parser.cpp
  ${SDK_NETWORK}/CatalogList/src/XmlListParser.cpp
  ${REPO_ROOT}/lib/expat/ExpatCompat.cpp
  ${REPO_ROOT}/lib/expat/ExpatErrors.cpp)
target_include_directories(ExpatSdkCatalogTest PRIVATE
  ${SDK_NETWORK}/CatalogList/include ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/test/koreader_xpath_resolver/stubs)
target_compile_options(ExpatSdkCatalogTest PRIVATE -UNDEBUG)
target_link_libraries(ExpatSdkCatalogTest PRIVATE xp_ffi_host)
add_test(NAME ExpatSdkCatalog COMMAND ExpatSdkCatalogTest)

add_test(NAME ExpatSdkApiBaseline COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/oracle.py
  --binary $<TARGET_FILE:ExpatConsumerBaseline> --api)
add_test(NAME ExpatSdkApiAdapter COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/oracle.py
  --binary $<TARGET_FILE:ExpatConsumerAdapter> --api)
add_executable(ExpatAdapterTest AdapterTest.cpp
  ${REPO_ROOT}/lib/expat/ExpatCompat.cpp ${REPO_ROOT}/lib/expat/ExpatErrors.cpp)
target_include_directories(ExpatAdapterTest PRIVATE ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/test/koreader_xpath_resolver/stubs)
target_compile_definitions(ExpatAdapterTest PRIVATE XP_FFI_HOST_FAULT_INJECTION)
target_link_libraries(ExpatAdapterTest PRIVATE xp_ffi_host GTest::gtest_main)
gtest_discover_tests(ExpatAdapterTest)
