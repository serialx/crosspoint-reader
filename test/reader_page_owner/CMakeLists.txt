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

# Keep the actual child declarations and owned members; replace only their
# scheduling/UI dependencies for construction/destruction fault coverage.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/physical/activities)
configure_file(${REPO_ROOT}/src/activities/OwnedLegacyActivity.h
  ${CMAKE_CURRENT_BINARY_DIR}/physical/activities/OwnedLegacyActivity.h COPYONLY)
configure_file(transfer_stubs/activities/Activity.h
  ${CMAKE_CURRENT_BINARY_DIR}/physical/activities/Activity.h COPYONLY)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ClipConstructors.cpp
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract_constructors.py
    ${CMAKE_CURRENT_BINARY_DIR}/ClipConstructors.cpp
  DEPENDS extract_constructors.py ${REPO_ROOT}/src/activities/reader/ClipSelectionActivity.cpp
    ${REPO_ROOT}/test/reader_lifecycle/test_popup_resource.py
  VERBATIM)
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract_frame.py
  ${CMAKE_CURRENT_BINARY_DIR} COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  extract_frame.py Frame.cpp.in frame_renderer.inc
  ${REPO_ROOT}/test/reader_render_resources/parent_frame.inc
  ${REPO_ROOT}/test/reader_lifecycle/test_popup_resource.py
  ${REPO_ROOT}/test/section_golden/stubs/GfxRenderer.h)
add_executable(ReaderPageTransfer PageTransfer.cpp ${READER_PAGE_SOURCES}
  ${CMAKE_CURRENT_BINARY_DIR}/ClipConstructors.cpp ${CMAKE_CURRENT_BINARY_DIR}/PageFrame.cpp)
target_include_directories(ReaderPageTransfer PRIVATE
  ${REPO_ROOT}/test ${REPO_ROOT}/test/reader_lifecycle/native_render
  ${CMAKE_CURRENT_BINARY_DIR}/physical ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/transfer_stubs
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs ${PAGE_CODEC_INCLUDES} ${REPO_ROOT}/test/ui_runtime)
target_compile_definitions(ReaderPageTransfer PRIVATE XP_EPUB_PAGE_OWNER_TEST=1)
target_compile_options(ReaderPageTransfer PRIVATE -UNDEBUG)
target_link_libraries(ReaderPageTransfer PRIVATE crosspoint_test_common xp_ffi_host)
add_test(NAME ReaderPageTransfer COMMAND ${Python3_EXECUTABLE}
  ${REPO_ROOT}/test/page_codec_diff/run.py --dump $<TARGET_FILE:ReaderPageTransfer>)

# Frozen parent frame and real Rust Page owner, with the legacy strip oracle.
if(XP_FFI_HOST_FEATURES MATCHES "(^|,)reader-strips(,|$)")
  add_executable(ReaderPageFrameStrip PageTransfer.cpp ${READER_PAGE_SOURCES}
    ${CMAKE_CURRENT_BINARY_DIR}/ClipConstructors.cpp ${CMAKE_CURRENT_BINARY_DIR}/PageFrame.cpp)
  target_include_directories(ReaderPageFrameStrip PRIVATE
    ${REPO_ROOT}/test ${REPO_ROOT}/test/reader_lifecycle/native_render
    ${CMAKE_CURRENT_BINARY_DIR}/physical ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/transfer_stubs
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs ${PAGE_CODEC_INCLUDES} ${REPO_ROOT}/test/ui_runtime)
  target_compile_definitions(ReaderPageFrameStrip PRIVATE XP_EPUB_PAGE_OWNER_TEST=1 XP_STRIP_RENDER=1)
  target_compile_options(ReaderPageFrameStrip PRIVATE -UNDEBUG)
  target_link_libraries(ReaderPageFrameStrip PRIVATE crosspoint_test_common xp_ffi_host)
  add_test(NAME ReaderPageFrameStrip COMMAND ${Python3_EXECUTABLE}
    ${REPO_ROOT}/test/page_codec_diff/run.py --dump $<TARGET_FILE:ReaderPageFrameStrip>)
endif()
