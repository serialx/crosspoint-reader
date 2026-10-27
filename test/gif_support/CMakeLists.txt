find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_executable(GifSupportTest GifSupportTest.cpp GifDecoderTest.cpp)
target_include_directories(GifSupportTest PRIVATE
  ${REPO_ROOT}/lib/GifSupport
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs)
target_link_libraries(GifSupportTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(GifSupportTest)

add_executable(GifBmpTest GifBmpTest.cpp
  ${REPO_ROOT}/lib/GifToBmpConverter/GifToBmpConverter.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp)
target_include_directories(GifBmpTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/lib/GifToBmpConverter
  ${REPO_ROOT}/lib/GifSupport
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Memory)
target_link_libraries(GifBmpTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(GifBmpTest)

add_executable(ImageCacheTest ${REPO_ROOT}/test/image_golden/CacheTest.cpp)
target_include_directories(ImageCacheTest PRIVATE
  ${REPO_ROOT}/test/image_golden/stubs
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/lib/Epub/Epub/converters)
target_link_libraries(ImageCacheTest PRIVATE crosspoint_test_common GTest::gtest_main xp_ffi_host)
gtest_discover_tests(ImageCacheTest)

# Optional differential check against an externally preserved, patched decoder.
set(ANIMATEDGIF_SOURCE_DIR "" CACHE PATH "Optional pinned patched AnimatedGIF source")
if(ANIMATEDGIF_SOURCE_DIR)
  add_executable(GifLegacyTest LegacyDecoderTest.cpp ${ANIMATEDGIF_SOURCE_DIR}/src/AnimatedGIF.cpp)
  target_include_directories(GifLegacyTest PRIVATE ${ANIMATEDGIF_SOURCE_DIR}/src
    ${REPO_ROOT}/lib/GifSupport ${REPO_ROOT}/test/chapter_html_slim_parser/stubs)
  target_compile_definitions(GifLegacyTest PRIVATE __LINUX__ MAX_WIDTH=3072)
  target_link_libraries(GifLegacyTest PRIVATE crosspoint_test_common GTest::gtest_main)
  gtest_discover_tests(GifLegacyTest TEST_PREFIX Legacy.)
endif()

add_executable(gif_oracle Oracle.cpp ${REPO_ROOT}/lib/GifToBmpConverter/GifToBmpConverter.cpp)
target_include_directories(gif_oracle PRIVATE stubs ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/lib/GifSupport ${REPO_ROOT}/lib/GifToBmpConverter)
target_link_libraries(gif_oracle PRIVATE xp_ffi_host)

add_executable(gif_frame_oracle ${REPO_ROOT}/test/image_golden/Oracle.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/GifToFramebufferConverter.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp)
target_compile_definitions(gif_frame_oracle PRIVATE GIF_ONLY)
target_include_directories(gif_frame_oracle PRIVATE ${REPO_ROOT}/test/image_golden/stubs
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs ${REPO_ROOT}/lib/GifSupport
  ${REPO_ROOT}/lib/Epub/Epub/converters)
target_link_libraries(gif_frame_oracle PRIVATE xp_ffi_host)
add_test(NAME GifFrozenMatrix COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/matrix.py $<TARGET_FILE:gif_frame_oracle>)
add_test(NAME GifMutations COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/oracle.py $<TARGET_FILE:gif_oracle> $<TARGET_FILE:gif_frame_oracle> --allow-documented)

add_executable(GifAllocationTest AllocationTest.cpp)
target_compile_definitions(GifAllocationTest PRIVATE XP_FFI_HOST_FAULT_INJECTION)
target_link_libraries(GifAllocationTest PRIVATE xp_ffi_host GTest::gtest_main)
gtest_discover_tests(GifAllocationTest)
