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
target_include_directories(BookOracle PRIVATE epub-stubs
 ${REPO_ROOT}/lib/Epub ${REPO_ROOT}/lib/ZipFile
 ${REPO_ROOT}/lib/FsHelpers ${REPO_ROOT}/lib/Utf8
 ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/Serialization
 ${REPO_ROOT}/lib/hal ${REPO_ROOT}/lib/miniz/src
 ${REPO_ROOT}/lib/GifSupport ${REPO_ROOT}/lib/XmlParserUtils ${REPO_ROOT}/lib/expat
 ${REPO_ROOT}/test/chapter_html_slim_parser/stubs)
get_filename_component(BOOK_IO_SOURCE_DIR "${BOOK_IO_EPUB_SOURCE}" DIRECTORY)
target_include_directories(BookOracle BEFORE PRIVATE "${BOOK_IO_SOURCE_DIR}")
target_compile_options(BookOracle PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-include;algorithm;-ftrivial-auto-var-init=zero>")
target_link_libraries(BookOracle PRIVATE xp_ffi_host)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME BookIoGolden COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py --oracle $<TARGET_FILE:BookOracle>)
add_executable(BookXtcOracle XtcOracle.cpp
  ${REPO_ROOT}/lib/Xtc/Xtc.cpp ${REPO_ROOT}/lib/Xtc/Xtc/XtcParser.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp)
target_include_directories(BookXtcOracle PRIVATE stubs
  ${REPO_ROOT}/test/xtc_diff/stubs ${REPO_ROOT}/lib/Xtc ${REPO_ROOT}/lib/GfxRenderer)
target_link_libraries(BookXtcOracle PRIVATE xp_ffi_host)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME BookXtcOracle COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/xtc_oracle.py $<TARGET_FILE:BookXtcOracle>)
add_executable(BookKeyOracle BookKeyOracle.cpp ${REPO_ROOT}/lib/Epub/BookKey.cpp)
target_include_directories(BookKeyOracle PRIVATE stubs ${REPO_ROOT}/lib/Epub ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs)
target_link_libraries(BookKeyOracle PRIVATE xp_ffi_host)
add_test(NAME BookKeyOracle COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/key_oracle.py $<TARGET_FILE:BookKeyOracle>)

add_test(NAME BookXtcNegativeStream COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/xtc_oracle.py $<TARGET_FILE:BookXtcOracle> --negative-stream)

add_test(NAME BookXtcNegativeCopy COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/xtc_oracle.py $<TARGET_FILE:BookXtcOracle> --negative-copy)
add_executable(BookTxtOracle TxtOracle.cpp ${REPO_ROOT}/lib/Txt/Txt.cpp ${REPO_ROOT}/lib/Txt/TxtToHtml.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp)
target_include_directories(BookTxtOracle PRIVATE txt-stubs stubs ${REPO_ROOT}/lib/Txt ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/FsHelpers ${REPO_ROOT}/lib/Utf8 ${REPO_ROOT}/test/fs_helpers/stubs
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs)
target_link_libraries(BookTxtOracle PRIVATE xp_ffi_host)
add_test(NAME BookTxtOracle COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/txt_oracle.py $<TARGET_FILE:BookTxtOracle>)
add_executable(BookHelpersOracle HelpersOracle.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp)
target_include_directories(BookHelpersOracle PRIVATE ${REPO_ROOT}/lib/AsciiText
  ${REPO_ROOT}/lib/XmlParserUtils ${REPO_ROOT}/lib/Utf8)
target_link_libraries(BookHelpersOracle PRIVATE xp_ffi_host)
add_test(NAME BookHelpersOracle COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/helpers_oracle.py $<TARGET_FILE:BookHelpersOracle>)

add_test(NAME BookLoanOracle COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/loan_oracle.py ${XP_FFI_HOST_ARCHIVE})

add_executable(BookTxtCallbacksTest TxtCallbacksTest.cpp)
target_link_libraries(BookTxtCallbacksTest PRIVATE xp_ffi_host GTest::gtest_main)
gtest_discover_tests(BookTxtCallbacksTest)

add_test(NAME BookResidualOracle COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/residual_oracle.py --oracle $<TARGET_FILE:BookOracle>)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/RecoveryOracle.cpp
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/recovery_oracle.py
    ${REPO_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/RecoveryOracle.cpp
  DEPENDS recovery_oracle.py ${REPO_ROOT}/lib/Epub/Epub/Paragraph.cpp
    ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterPageServices.cpp VERBATIM)
add_executable(BookRecoveryOracle ${CMAKE_CURRENT_BINARY_DIR}/RecoveryOracle.cpp)
target_include_directories(BookRecoveryOracle PRIVATE ${REPO_ROOT}/lib/Memory)
target_link_libraries(BookRecoveryOracle PRIVATE xp_ffi_host)
add_test(NAME BookRecoveryOracle COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/frozen_trace.py $<TARGET_FILE:BookRecoveryOracle>
  ${CMAKE_CURRENT_SOURCE_DIR}/recovery-golden.txt.gz)
add_executable(BookBidiEligibilityOracle BidiEligibilityOracle.cpp ${REPO_ROOT}/lib/MiniBidi/RustBidi.cpp)
target_include_directories(BookBidiEligibilityOracle PRIVATE ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/test/minibidi_arabic/stubs)
target_link_libraries(BookBidiEligibilityOracle PRIVATE xp_ffi_host)
add_test(NAME BookBidiEligibilityOracle COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/frozen_trace.py $<TARGET_FILE:BookBidiEligibilityOracle>
  ${CMAKE_CURRENT_SOURCE_DIR}/bidi-eligibility-golden.txt.gz)
add_test(NAME BookGetterOracle COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/getters_oracle.py --oracle $<TARGET_FILE:BookOracle>)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ReclaimOracle.cpp
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/reclaim_oracle.py
    ${REPO_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/ReclaimOracle.cpp
  DEPENDS reclaim_oracle.py ${REPO_ROOT}/lib/Epub/ContentProtection.cpp VERBATIM)
add_executable(BookReclaimOracle ${CMAKE_CURRENT_BINARY_DIR}/ReclaimOracle.cpp)
target_link_libraries(BookReclaimOracle PRIVATE xp_ffi_host)
add_test(NAME BookReclaimOracle COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/frozen_trace.py $<TARGET_FILE:BookReclaimOracle>
  ${CMAKE_CURRENT_SOURCE_DIR}/reclaim-golden.txt.gz)
