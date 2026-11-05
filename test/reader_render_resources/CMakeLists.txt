cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(reader_render_resources NONE)
  enable_testing()
endif()
find_program(RENDER_RESOURCES_CARGO cargo REQUIRED)
foreach(mode IN ITEMS default strips)
  set(features)
  set(name ReaderRenderResources)
  if(mode STREQUAL strips)
    set(features --features reader-strips)
    set(name ReaderRenderResourcesStrips)
  endif()
  add_test(NAME ${name} COMMAND ${RENDER_RESOURCES_CARGO} test
    --manifest-path ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml
    --target-dir ${CMAKE_CURRENT_BINARY_DIR}/${mode}
    --locked --offline --lib ${features})
endforeach()
