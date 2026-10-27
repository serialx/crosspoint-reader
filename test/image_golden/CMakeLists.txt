cmake_minimum_required(VERSION 3.16)
project(image_golden C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_BUILD_TYPE Release)
get_filename_component(REPO_ROOT "../.." ABSOLUTE)
set(DECODER_SOURCE_DIR "" CACHE PATH "Existing patched PlatformIO libdeps directory")
foreach(lib JPEGDEC AnimatedGIF)
  if(NOT EXISTS "${DECODER_SOURCE_DIR}/${lib}/src/${lib}.cpp")
    message(FATAL_ERROR "Set DECODER_SOURCE_DIR to patched firmware libdeps")
  endif()
endforeach()
add_executable(image_oracle Oracle.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/PngToFramebufferConverter.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/JpegToFramebufferConverter.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/GifToFramebufferConverter.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${DECODER_SOURCE_DIR}/JPEGDEC/src/JPEGDEC.cpp
  ${DECODER_SOURCE_DIR}/AnimatedGIF/src/AnimatedGIF.cpp)
target_compile_definitions(image_oracle PRIVATE __LINUX__ MAX_WIDTH=3072)
target_include_directories(image_oracle PRIVATE stubs
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/lib/Epub/Epub/converters ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/GifSupport
  ${DECODER_SOURCE_DIR}/JPEGDEC/src ${DECODER_SOURCE_DIR}/AnimatedGIF/src)
option(IMAGE_ASAN "Instrument host oracle" OFF)
if(IMAGE_ASAN)
  target_compile_options(image_oracle PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(image_oracle PRIVATE -fsanitize=address,undefined)
endif()

include(${REPO_ROOT}/test/cmake/xp_ffi_host.cmake)
target_link_libraries(image_oracle PRIVATE xp_ffi_host)
add_executable(image_failures FailureTest.cpp)
target_link_libraries(image_failures PRIVATE xp_ffi_host)
target_compile_definitions(image_failures PRIVATE XP_FFI_HOST_FAULT_INJECTION)
enable_testing()
add_test(NAME ImagePipelineFailures COMMAND image_failures)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME ImagePipelineGoldens COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py $<TARGET_FILE:image_oracle>)

add_test(NAME ImagePipelineCaseKeys COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_run.py)

add_executable(png_bmp_oracle PngBmpOracle.cpp
  ${REPO_ROOT}/lib/PngToBmpConverter/PngToBmpConverter.cpp
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp)
target_include_directories(png_bmp_oracle PRIVATE stubs
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/lib/PngToBmpConverter ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/miniz/src ${REPO_ROOT}/lib/Memory)
target_link_libraries(png_bmp_oracle PRIVATE xp_ffi_host)

get_target_property(PNG_BENCH_SOURCES image_oracle SOURCES)
list(REMOVE_ITEM PNG_BENCH_SOURCES Oracle.cpp)
add_executable(png_benchmark PngBenchmark.cpp ${PNG_BENCH_SOURCES})
get_target_property(PNG_BENCH_INCLUDES image_oracle INCLUDE_DIRECTORIES)
target_include_directories(png_benchmark PRIVATE ${PNG_BENCH_INCLUDES})
target_compile_definitions(png_benchmark PRIVATE __LINUX__ MAX_WIDTH=3072)
target_link_libraries(png_benchmark PRIVATE xp_ffi_host)

# Optional pinned da609bbf export, kept outside the repository.
set(PNGDEC_REFERENCE_DIR "" CACHE PATH "PNGdec plus lib/ export captured before the Rust replacement")
if(PNGDEC_REFERENCE_DIR)
  set(PNG_REF "${PNGDEC_REFERENCE_DIR}")
  file(GLOB PNG_REF_C "${PNG_REF}/PNGdec/src/*.c")
  set(PNG_REF_SOURCES ${PNG_REF_C}
    ${PNG_REF}/lib/Epub/Epub/converters/PngToFramebufferConverter.cpp
    ${PNG_REF}/PNGdec/src/PNGdec.cpp ${PNG_REF}/lib/PngZlib/PngZlib.cpp
    ${PNG_REF}/lib/miniz/src/miniz_impl.c ${PNG_REF}/lib/miniz/src/MinizCompat.cpp)
  get_target_property(PNG_REFERENCE_SOURCES image_oracle SOURCES)
  list(REMOVE_ITEM PNG_REFERENCE_SOURCES
    ${REPO_ROOT}/lib/Epub/Epub/converters/PngToFramebufferConverter.cpp)
  add_executable(image_oracle_reference ${PNG_REFERENCE_SOURCES} ${PNG_REF_SOURCES})
  target_include_directories(image_oracle_reference PRIVATE ${PNG_BENCH_INCLUDES}
    ${PNG_REF}/PNGdec/src ${PNG_REF}/lib/PngZlib ${PNG_REF}/lib/miniz/src)
  target_compile_definitions(image_oracle_reference PRIVATE __LINUX__ MAX_WIDTH=3072 PNG_MAX_BUFFERED_PIXELS=16416)
  target_link_libraries(image_oracle_reference PRIVATE xp_ffi_host)
endif()

add_test(NAME PngConsumerGoldens COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/png_run.py ${CMAKE_CURRENT_BINARY_DIR})

add_executable(png_resources PngResources.cpp)
add_dependencies(png_resources xp_ffi_host_build)
target_include_directories(png_resources PRIVATE ${REPO_ROOT}/rust/crates/xp-ffi/include)
target_link_libraries(png_resources PRIVATE ${XP_FFI_HOST_ARCHIVE})

add_executable(png_header_test PngHeaderTest.cpp)
target_link_libraries(png_header_test PRIVATE xp_ffi_host)
add_test(NAME PngSleepHeader COMMAND png_header_test)
add_test(NAME PngResources COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/png_resources.py $<TARGET_FILE:png_resources>)
