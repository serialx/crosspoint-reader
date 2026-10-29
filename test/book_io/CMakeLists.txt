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
