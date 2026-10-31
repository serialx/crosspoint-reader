cmake_minimum_required(VERSION 3.16)
project(package_golden LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/instrument.py
  ${REPO_ROOT}/lib/Epub/Epub/BookMetadataCache.cpp ${CMAKE_CURRENT_BINARY_DIR}/BookMetadataCache.cpp
  COMMAND_ERROR_IS_FATAL ANY)
add_executable(PackageOracle
  PackageOracle.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/PackageParser.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/BookMetadataCache.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(PackageOracle PRIVATE
  ${REPO_ROOT}/test/xml_oracle/stubs
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
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
target_compile_options(PackageOracle PRIVATE -Wall -Wextra)
include(${REPO_ROOT}/test/cmake/xp_ffi_host.cmake)
target_link_libraries(PackageOracle PRIVATE xp_ffi_host)
add_test(NAME PackageGoldens COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py --oracle $<TARGET_FILE:PackageOracle> verify)
add_executable(PackageAbiTest PackageAbiTest.cpp)
target_link_libraries(PackageAbiTest PRIVATE xp_ffi_host)
target_compile_definitions(PackageAbiTest PRIVATE XP_FFI_HOST_FAULT_INJECTION)
target_compile_options(PackageAbiTest PRIVATE -UNDEBUG)
add_test(NAME PackageAllocationFailure COMMAND PackageAbiTest)

add_executable(TocBoundaryTest TocBoundaryTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/PackageParser.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/Epub/Epub/BookMetadataCache.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp)
get_target_property(PACKAGE_INCLUDES PackageOracle INCLUDE_DIRECTORIES)
target_include_directories(TocBoundaryTest PRIVATE ${PACKAGE_INCLUDES}
  ${REPO_ROOT}/rust/crates/xp-ffi/include)
add_dependencies(TocBoundaryTest xp_ffi_host_build)
target_link_libraries(TocBoundaryTest PRIVATE ${XP_FFI_HOST_ARCHIVE})
add_test(NAME TocBoundaryAllocation COMMAND TocBoundaryTest ${CMAKE_CURRENT_BINARY_DIR}/toc-boundary)

add_executable(PackageWriterTest PackageWriterTest.cpp)
target_link_libraries(PackageWriterTest PRIVATE xp_ffi_host)
target_compile_definitions(PackageWriterTest PRIVATE XP_FFI_HOST_FAULT_INJECTION)
target_compile_options(PackageWriterTest PRIVATE -UNDEBUG)
add_test(NAME PackageWriterFailure COMMAND PackageWriterTest)

# Capture the production formatter, including errors returned by the Rust XML core.
foreach(target PackageOracle TocBoundaryTest)
  target_sources(${target} PRIVATE ${REPO_ROOT}/lib/expat/ExpatErrors.cpp)
  target_include_directories(${target} BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/logging ${REPO_ROOT}/lib/expat)
endforeach()
add_test(NAME PackageDiagnosticGoldens COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/diagnostics.py --oracle $<TARGET_FILE:PackageOracle>)

add_executable(PackageDiagnosticsTest PackageDiagnosticsTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/PackageParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/BookMetadataCache.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/expat/ExpatErrors.cpp)
target_include_directories(PackageDiagnosticsTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/logging ${REPO_ROOT}/lib/expat ${PACKAGE_INCLUDES})
target_link_libraries(PackageDiagnosticsTest PRIVATE xp_ffi_host)
target_compile_definitions(PackageDiagnosticsTest PRIVATE XP_FFI_HOST_FAULT_INJECTION)
target_compile_options(PackageDiagnosticsTest PRIVATE -UNDEBUG)
add_test(NAME PackageDiagnosticFailures COMMAND PackageDiagnosticsTest ${CMAKE_CURRENT_BINARY_DIR}/diagnostic-failures)

add_executable(ZipDiagnosticsTest ZipDiagnosticsTest.cpp
  ${REPO_ROOT}/lib/ZipFile/ZipFile.cpp
  ${REPO_ROOT}/lib/miniz/src/InflateStream.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/lib/hal/HalMemory.cpp)
target_include_directories(ZipDiagnosticsTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/zip_logging
  ${REPO_ROOT}/test/zip_file/stubs ${REPO_ROOT}/lib/ZipFile ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/hal ${REPO_ROOT}/lib/miniz/src)
target_link_libraries(ZipDiagnosticsTest PRIVATE xp_ffi_host)
target_compile_options(ZipDiagnosticsTest PRIVATE -UNDEBUG -fno-access-control)
add_test(NAME ZipDiagnosticFailures COMMAND ZipDiagnosticsTest)
