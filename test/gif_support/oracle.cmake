add_executable(gif_oracle ${REPO_ROOT}/test/gif_support/Oracle.cpp
  ${REPO_ROOT}/lib/GifToBmpConverter/GifToBmpConverter.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp)
target_include_directories(gif_oracle PRIVATE
  ${REPO_ROOT}/test/gif_support/stubs
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/lib/GifToBmpConverter ${REPO_ROOT}/lib/GifSupport
  ${REPO_ROOT}/lib/GfxRenderer ${REPO_ROOT}/lib/Memory
  ${DECODER_SOURCE_DIR}/AnimatedGIF/src)
target_sources(gif_oracle PRIVATE ${DECODER_SOURCE_DIR}/AnimatedGIF/src/AnimatedGIF.cpp)
target_compile_definitions(gif_oracle PRIVATE __LINUX__ MAX_WIDTH=3072)
target_link_libraries(gif_oracle PRIVATE xp_ffi_host)
