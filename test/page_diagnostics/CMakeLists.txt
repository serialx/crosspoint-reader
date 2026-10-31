get_target_property(PAGE_DIAGNOSTIC_SOURCES PageCodecDiff SOURCES)
list(FILTER PAGE_DIAGNOSTIC_SOURCES EXCLUDE REGEX "(^PageCodecDiff.cpp$|/Serialization.cpp$|/TextSerialization.cpp$)")
get_target_property(PAGE_DIAGNOSTIC_INCLUDES SectionGoldenDump INCLUDE_DIRECTORIES)
add_executable(PageDiagnosticProbe PageProbe.cpp ${PAGE_DIAGNOSTIC_SOURCES})
target_include_directories(PageDiagnosticProbe PRIVATE stubs ${REPO_ROOT}/test/page_codec_diff/stubs ${PAGE_DIAGNOSTIC_INCLUDES})
target_link_libraries(PageDiagnosticProbe PRIVATE crosspoint_test_common xp_ffi_host)
target_compile_definitions(PageDiagnosticProbe PRIVATE XP_FFI_HOST_FAULT_INJECTION=1)
add_test(NAME PageDiagnostics COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py $<TARGET_FILE:PageDiagnosticProbe>)

get_target_property(SECTION_DIAGNOSTIC_SOURCES PageLoad SOURCES)
list(REMOVE_ITEM SECTION_DIAGNOSTIC_SOURCES PageLoad.cpp)
add_executable(SectionDiagnosticProbe SectionProbe.cpp ${SECTION_DIAGNOSTIC_SOURCES})
target_include_directories(SectionDiagnosticProbe PRIVATE file_stubs stubs ${PAGE_DIAGNOSTIC_INCLUDES})
target_link_libraries(SectionDiagnosticProbe PRIVATE crosspoint_test_common xp_ffi_host)
add_test(NAME SectionDiagnostics COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/section.py $<TARGET_FILE:SectionDiagnosticProbe>)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ReaderDiagnostic.cpp
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract_reader.py
    ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp ${CMAKE_CURRENT_BINARY_DIR}/ReaderDiagnostic.cpp
  DEPENDS extract_reader.py ${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp
  VERBATIM)
target_sources(PageDiagnosticProbe PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/ReaderDiagnostic.cpp)
