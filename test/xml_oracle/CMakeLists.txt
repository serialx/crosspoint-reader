cmake_minimum_required(VERSION 3.16)
project(xml_oracle LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_executable(XmlOracle
  XmlOracle.cpp
  ${REPO_ROOT}/test/vendor/expat/xmlparse.c
  ${REPO_ROOT}/test/vendor/expat/xmlrole.c
  ${REPO_ROOT}/test/vendor/expat/xmltok.c
)
target_include_directories(XmlOracle PRIVATE ${REPO_ROOT}/test/vendor/expat)
target_compile_definitions(XmlOracle PRIVATE XML_GE=0 XML_CONTEXT_BYTES=0)
target_compile_options(XmlOracle PRIVATE -Wall -Wextra -Werror)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME XmlOracleGoldens COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/oracle.py --oracle $<TARGET_FILE:XmlOracle> verify)

add_executable(XmlConsumerOracle
  XmlConsumerOracle.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/PackageParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/BookMetadataCache.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(XmlConsumerOracle PRIVATE
  stubs
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub/parsers
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/test/fs_helpers/stubs
  ${REPO_ROOT}/test/book_metadata_cache/stubs
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/test/content_opf_parser/stubs
)
target_compile_options(XmlConsumerOracle PRIVATE -Wall -Wextra)
add_test(NAME XmlConsumerGoldens COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/consumers.py --oracle $<TARGET_FILE:XmlConsumerOracle> verify)
include(${REPO_ROOT}/test/cmake/xp_ffi_host.cmake)
target_link_libraries(XmlConsumerOracle PRIVATE xp_ffi_host)
add_executable(XmlAbiTest XmlAbiTest.cpp)
target_link_libraries(XmlAbiTest PRIVATE xp_ffi_host)
target_compile_definitions(XmlAbiTest PRIVATE XP_FFI_HOST_FAULT_INJECTION)
target_compile_options(XmlAbiTest PRIVATE -UNDEBUG)
add_test(NAME XmlAbiAllocationFailure COMMAND XmlAbiTest)

target_include_directories(XmlAbiTest PRIVATE ${REPO_ROOT}/lib/XmlParserUtils ${REPO_ROOT}/test/chapter_html_slim_parser/stubs)

add_test(NAME XmlRegressionGoldens COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/regressions.py --oracle $<TARGET_FILE:XmlOracle> verify)

target_sources(XmlConsumerOracle PRIVATE ${REPO_ROOT}/lib/expat/ExpatErrors.cpp)
target_include_directories(XmlConsumerOracle PRIVATE ${REPO_ROOT}/lib/expat)
