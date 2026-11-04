find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_target_property(PAGE_CODEC_INCLUDES SectionGoldenDump INCLUDE_DIRECTORIES)
get_target_property(READER_PAGE_SOURCES PageCodecDiff SOURCES)
list(FILTER READER_PAGE_SOURCES EXCLUDE REGEX "PageCodecDiff.cpp|Serialization.cpp")
add_executable(ReaderPageOwner PageOwner.cpp ${READER_PAGE_SOURCES})
target_include_directories(ReaderPageOwner PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs ${PAGE_CODEC_INCLUDES}
  ${REPO_ROOT}/test/ui_runtime)
target_compile_options(ReaderPageOwner PRIVATE -UNDEBUG)
target_link_libraries(ReaderPageOwner PRIVATE crosspoint_test_common xp_ffi_host)
add_test(NAME ReaderPageOwner COMMAND ${Python3_EXECUTABLE}
  ${REPO_ROOT}/test/page_codec_diff/run.py --dump $<TARGET_FILE:ReaderPageOwner>)
add_test(NAME ImageScaleFormatting COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/scale_oracle.py $<TARGET_FILE:ReaderPageOwner>)
