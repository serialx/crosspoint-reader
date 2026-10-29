cmake_minimum_required(VERSION 3.20)
project(book_io LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
enable_testing()
include(${REPO_ROOT}/test/cmake/xp_ffi_host.cmake)
set(BOOK_IO_EPUB_SOURCE "${REPO_ROOT}/lib/Epub/Epub.cpp" CACHE FILEPATH "EPUB source for differential captures")
add_executable(BookOracle BookOracle.cpp
 ${BOOK_IO_EPUB_SOURCE}
 ${REPO_ROOT}/lib/Epub/Epub/BookMetadataCache.cpp
 ${REPO_ROOT}/lib/Epub/Epub/parsers/PackageParser.cpp
 ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp
 ${REPO_ROOT}/lib/ZipFile/ZipFile.cpp
 ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
 ${REPO_ROOT}/lib/Utf8/Utf8.cpp
 ${REPO_ROOT}/lib/miniz/src/InflateStream.cpp
 ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
 ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
 ${REPO_ROOT}/lib/hal/HalMemory.cpp
 ${REPO_ROOT}/lib/expat/ExpatErrors.cpp)
target_include_directories(BookOracle PRIVATE stubs
 ${REPO_ROOT}/lib/Epub
 ${REPO_ROOT}/lib/Epub ${REPO_ROOT}/lib/ZipFile
 ${REPO_ROOT}/lib/FsHelpers ${REPO_ROOT}/lib/Utf8
 ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/Serialization
 ${REPO_ROOT}/lib/hal ${REPO_ROOT}/lib/miniz/src
 ${REPO_ROOT}/lib/GifSupport ${REPO_ROOT}/lib/XmlParserUtils ${REPO_ROOT}/lib/expat
 ${REPO_ROOT}/test/chapter_html_slim_parser/stubs)
target_compile_options(BookOracle PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-include;algorithm;-ftrivial-auto-var-init=zero>")
target_link_libraries(BookOracle PRIVATE xp_ffi_host)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME BookIoGolden COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py --oracle $<TARGET_FILE:BookOracle>)
