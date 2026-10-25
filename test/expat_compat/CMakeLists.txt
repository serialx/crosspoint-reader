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
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c)
target_include_directories(ExpatConsumerBaseline PRIVATE
  stubs ${SDK_NETWORK}/Opds/include ${SDK_NETWORK}/CatalogList/include
  ${SDK_NETWORK}/JsonSax/include ${REPO_ROOT}/lib/expat)
target_compile_definitions(ExpatConsumerBaseline PRIVATE XML_GE=0 XML_CONTEXT_BYTES=0)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME ExpatSdkBaseline COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/oracle.py
  --binary $<TARGET_FILE:ExpatConsumerBaseline>)
