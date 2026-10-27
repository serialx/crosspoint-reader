add_executable(InflateStreamTest
  InflateStreamTest.cpp
  InflateGoldenTest.cpp
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/lib/hal/HalMemory.cpp
)
target_include_directories(InflateStreamTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/lib/miniz/src
)
target_link_libraries(InflateStreamTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(InflateStreamTest)

target_compile_definitions(InflateStreamTest PRIVATE INFLATE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(MINIZ_COMPAT_GOLDEN "${CMAKE_CURRENT_BINARY_DIR}/mz-golden.bin")
add_custom_command(OUTPUT "${MINIZ_COMPAT_GOLDEN}"
  COMMAND ${Python3_EXECUTABLE} -c
    "import gzip,pathlib,sys; pathlib.Path(sys.argv[2]).write_bytes(gzip.decompress(pathlib.Path(sys.argv[1]).read_bytes()))"
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/mz-golden.bin.gz" "${MINIZ_COMPAT_GOLDEN}"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/mz-golden.bin.gz"
  VERBATIM)
add_custom_target(miniz_compat_golden DEPENDS "${MINIZ_COMPAT_GOLDEN}")
add_executable(MinizCompatTest MinizCompatTest.cpp SdkMinizAbi.cpp ${REPO_ROOT}/lib/miniz/src/MinizCompat.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c)
target_include_directories(MinizCompatTest PRIVATE ${REPO_ROOT}/lib/miniz/src)
target_compile_definitions(MinizCompatTest PRIVATE INFLATE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_link_libraries(MinizCompatTest PRIVATE crosspoint_test_common GTest::gtest_main xp_ffi_host)
gtest_discover_tests(MinizCompatTest)

add_dependencies(MinizCompatTest miniz_compat_golden)
target_compile_definitions(MinizCompatTest PRIVATE MINIZ_COMPAT_GOLDEN="${MINIZ_COMPAT_GOLDEN}")

function(expand_inflate_fixture target input output)
  set(codec gzip)
  if(input MATCHES "\\.xz$")
    set(codec lzma)
  endif()
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${output}"
    COMMAND ${Python3_EXECUTABLE} -c
      "import ${codec},pathlib,sys; pathlib.Path(sys.argv[2]).write_bytes(${codec}.decompress(pathlib.Path(sys.argv[1]).read_bytes()))"
      "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${input}" "${CMAKE_CURRENT_BINARY_DIR}/${output}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${input}" VERBATIM)
  add_custom_target(${output}_expand DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${output}")
  add_dependencies(${target} ${output}_expand)
endfunction()
expand_inflate_fixture(InflateStreamTest golden.bin.gz golden.bin)
target_compile_definitions(InflateStreamTest PRIVATE INFLATE_GOLDEN="${CMAKE_CURRENT_BINARY_DIR}/golden.bin")
add_executable(PngInflateTest PngInflateTest.cpp
  ${REPO_ROOT}/lib/miniz/src/MinizCompat.cpp ${REPO_ROOT}/lib/miniz/src/miniz_impl.c)
target_include_directories(PngInflateTest PRIVATE ${REPO_ROOT}/lib/miniz/src)
target_link_libraries(PngInflateTest PRIVATE crosspoint_test_common GTest::gtest_main xp_ffi_host)
expand_inflate_fixture(PngInflateTest png-idat.bin.gz png-idat.bin)
expand_inflate_fixture(PngInflateTest png-trace.bin.xz png-trace.bin)
target_compile_definitions(PngInflateTest PRIVATE
  PNG_IDAT_FIXTURES="${CMAKE_CURRENT_BINARY_DIR}/png-idat.bin"
  PNG_INFLATE_GOLDEN="${CMAKE_CURRENT_BINARY_DIR}/png-trace.bin")
gtest_discover_tests(PngInflateTest)

add_executable(ZipInflateTest ZipCrcTest.cpp
  ${REPO_ROOT}/lib/ZipFile/ZipFile.cpp
  ${REPO_ROOT}/lib/miniz/src/InflateStream.cpp
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/lib/hal/HalMemory.cpp)
target_include_directories(ZipInflateTest PRIVATE
  ${REPO_ROOT}/test/zip_file/stubs ${REPO_ROOT}/lib/ZipFile
  ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/hal ${REPO_ROOT}/lib/miniz/src)
target_compile_definitions(ZipInflateTest PRIVATE EPUB_FIXTURES="${REPO_ROOT}/test/epubs")
target_link_libraries(ZipInflateTest PRIVATE crosspoint_test_common GTest::gtest_main xp_ffi_host)
gtest_discover_tests(ZipInflateTest)

add_executable(InflateMutationProbe MutationProbe.cpp
  ${REPO_ROOT}/lib/miniz/src/MinizCompat.cpp ${REPO_ROOT}/lib/miniz/src/miniz_impl.c)
target_include_directories(InflateMutationProbe PRIVATE ${REPO_ROOT}/lib/miniz/src)
target_link_libraries(InflateMutationProbe PRIVATE xp_ffi_host)
add_test(NAME InflateMutations COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/test_mutations.py $<TARGET_FILE:InflateMutationProbe>)
