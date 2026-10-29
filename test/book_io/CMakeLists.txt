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
