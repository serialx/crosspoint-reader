cmake_minimum_required(VERSION 3.16)
project(xml_oracle LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_executable(XmlOracle
  XmlOracle.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)
target_include_directories(XmlOracle PRIVATE ${REPO_ROOT}/lib/expat)
target_compile_definitions(XmlOracle PRIVATE XML_GE=0 XML_CONTEXT_BYTES=0)
target_compile_options(XmlOracle PRIVATE -Wall -Wextra -Werror)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME XmlOracleGoldens COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/oracle.py --oracle $<TARGET_FILE:XmlOracle> verify)
