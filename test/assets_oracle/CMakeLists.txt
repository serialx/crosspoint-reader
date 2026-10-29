cmake_minimum_required(VERSION 3.16)
project(assets_oracle C CXX)
set(CMAKE_CXX_STANDARD 20)
get_filename_component(REPO_ROOT "../.." ABSOLUTE)
set(DECODER_SOURCE_DIR "${REPO_ROOT}/.pio/libdeps/default" CACHE PATH "Patched firmware dependencies")
include(${REPO_ROOT}/test/cmake/xp_ffi_host.cmake)
add_executable(assets_oracle Oracle.cpp
  ${REPO_ROOT}/lib/JpegToBmpConverter/JpegToBmpConverter.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/src/util/ScreenshotUtil.cpp
  ${REPO_ROOT}/src/util/QrUtils.cpp
  ${DECODER_SOURCE_DIR}/JPEGDEC/src/JPEGDEC.cpp
  ${DECODER_SOURCE_DIR}/QRCode/src/qrcode.c)
target_compile_definitions(assets_oracle PRIVATE __LINUX__)
target_include_directories(assets_oracle PRIVATE stubs
  ${REPO_ROOT}/test/image_golden/stubs
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/lib/JpegToBmpConverter ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/Utf8 ${REPO_ROOT}/src/util
  ${DECODER_SOURCE_DIR}/JPEGDEC/src ${DECODER_SOURCE_DIR}/QRCode/src)
target_link_libraries(assets_oracle PRIVATE xp_ffi_host)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME AssetsGoldens COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py $<TARGET_FILE:assets_oracle>)
add_executable(image_block_oracle ImageBlockOracle.cpp ${REPO_ROOT}/lib/Epub/Epub/blocks/ImageBlock.cpp)
get_target_property(ASSET_INCLUDES assets_oracle INCLUDE_DIRECTORIES)
target_include_directories(image_block_oracle PRIVATE ${ASSET_INCLUDES} ${REPO_ROOT}/lib/Epub ${REPO_ROOT}/lib/Epub/Epub)
target_link_libraries(image_block_oracle PRIVATE xp_ffi_host)
add_test(NAME ImageBlockGoldens COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/image_block.py $<TARGET_FILE:image_block_oracle>)
get_target_property(ASSET_SOURCES assets_oracle SOURCES)
list(FILTER ASSET_SOURCES EXCLUDE REGEX "xp_ffi_host.cpp$")
add_executable(asset_allocations ${ASSET_SOURCES} Allocation.cpp)
target_compile_definitions(asset_allocations PRIVATE __LINUX__)
target_include_directories(asset_allocations PRIVATE ${ASSET_INCLUDES} ${REPO_ROOT}/rust/crates/xp-ffi/include)
add_dependencies(asset_allocations xp_ffi_host_build)
target_link_libraries(asset_allocations PRIVATE ${XP_FFI_HOST_ARCHIVE})
add_test(NAME AssetAllocationFailures COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/allocations.py $<TARGET_FILE:asset_allocations>)
