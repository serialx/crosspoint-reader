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
add_executable(image_block_allocations ImageBlockOracle.cpp Allocation.cpp ${REPO_ROOT}/lib/Epub/Epub/blocks/ImageBlock.cpp)
target_compile_definitions(image_block_allocations PRIVATE XP_ASSET_ALLOC_CHECK)
target_include_directories(image_block_allocations PRIVATE ${ASSET_INCLUDES} ${REPO_ROOT}/lib/Epub ${REPO_ROOT}/lib/Epub/Epub ${REPO_ROOT}/rust/crates/xp-ffi/include)
add_dependencies(image_block_allocations xp_ffi_host_build)
target_link_libraries(image_block_allocations PRIVATE ${XP_FFI_HOST_ARCHIVE})
add_test(NAME ImageCacheAllocations COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/contracts.py $<TARGET_FILE:image_block_allocations>)
add_executable(asset_contracts Contracts.cpp Allocation.cpp ${REPO_ROOT}/src/util/QrUtils.cpp ${DECODER_SOURCE_DIR}/QRCode/src/qrcode.c)
target_include_directories(asset_contracts PRIVATE ${ASSET_INCLUDES} ${REPO_ROOT}/rust/crates/xp-ffi/include)
add_dependencies(asset_contracts xp_ffi_host_build)
target_link_libraries(asset_contracts PRIVATE ${XP_FFI_HOST_ARCHIVE})
add_test(NAME AssetBufferContracts COMMAND asset_contracts)
target_compile_options(asset_contracts PRIVATE -UNDEBUG)
set(ASSET_REFERENCE_ROOT "" CACHE PATH "Optional ba6860d8 export for resource measurements")
if(ASSET_REFERENCE_ROOT)
  add_executable(asset_reference_allocations Oracle.cpp Allocation.cpp
    ${ASSET_REFERENCE_ROOT}/lib/JpegToBmpConverter/JpegToBmpConverter.cpp
    ${ASSET_REFERENCE_ROOT}/src/util/QrUtils.cpp ${ASSET_REFERENCE_ROOT}/src/util/ScreenshotUtil.cpp
    ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp
    ${DECODER_SOURCE_DIR}/JPEGDEC/src/JPEGDEC.cpp ${DECODER_SOURCE_DIR}/QRCode/src/qrcode.c)
  target_compile_definitions(asset_reference_allocations PRIVATE __LINUX__)
  target_include_directories(asset_reference_allocations PRIVATE ${ASSET_INCLUDES} ${REPO_ROOT}/rust/crates/xp-ffi/include)
  add_dependencies(asset_reference_allocations xp_ffi_host_build)
  target_link_libraries(asset_reference_allocations PRIVATE ${XP_FFI_HOST_ARCHIVE})
endif()

add_executable(image_asset_failures ImageFailures.cpp Allocation.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/ImageBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDecoderFactory.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/PngToFramebufferConverter.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/JpegToFramebufferConverter.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/GifToFramebufferConverter.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${DECODER_SOURCE_DIR}/JPEGDEC/src/JPEGDEC.cpp)
target_compile_definitions(image_asset_failures PRIVATE __LINUX__)
target_compile_options(image_asset_failures PRIVATE -UNDEBUG)
target_include_directories(image_asset_failures PRIVATE ${ASSET_INCLUDES}
  ${REPO_ROOT}/lib/Epub ${REPO_ROOT}/lib/Epub/Epub ${REPO_ROOT}/lib/PngDiagnostics
  ${REPO_ROOT}/lib/GifSupport ${REPO_ROOT}/rust/crates/xp-ffi/include)
add_dependencies(image_asset_failures xp_ffi_host_build)
target_link_libraries(image_asset_failures PRIVATE ${XP_FFI_HOST_ARCHIVE})
add_test(NAME ImageAssetFailurePaths COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/image_failures.py $<TARGET_FILE:image_asset_failures>)
