add_executable(gif_oracle ${REPO_ROOT}/test/gif_support/Oracle.cpp
  ${REPO_ROOT}/lib/GifToBmpConverter/GifToBmpConverter.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp)
target_include_directories(gif_oracle PRIVATE
  ${REPO_ROOT}/test/gif_support/stubs
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/lib/GifToBmpConverter ${REPO_ROOT}/lib/GifSupport
  ${REPO_ROOT}/lib/GfxRenderer ${REPO_ROOT}/lib/Memory
)
target_link_libraries(gif_oracle PRIVATE xp_ffi_host)
get_target_property(gif_benchmark_sources image_oracle SOURCES)
list(REMOVE_ITEM gif_benchmark_sources Oracle.cpp)
add_executable(gif_benchmark ${REPO_ROOT}/test/gif_support/Benchmark.cpp ${gif_benchmark_sources})
get_target_property(gif_benchmark_includes image_oracle INCLUDE_DIRECTORIES)
get_target_property(gif_benchmark_defines image_oracle COMPILE_DEFINITIONS)
target_include_directories(gif_benchmark PRIVATE ${gif_benchmark_includes})
target_compile_definitions(gif_benchmark PRIVATE ${gif_benchmark_defines})
target_link_libraries(gif_benchmark PRIVATE xp_ffi_host)
