# JPEGDEC header guard: the shipped patch set against the same set without the guard patch, under ASan and UBSan.
# Inherited decoder UB on every file is excluded: MOTOSHORT's unaligned 16-bit reads (alignment check off)
# and the Huffman lookup builder's negative shift exponents (ubsan-ignore.txt). Every other check stays on.
include(FetchContent)
FetchContent_Declare(jpeg_header_guard_jpegdec
  GIT_REPOSITORY https://github.com/bitbank2/JPEGDEC.git
  GIT_TAG 86282979224c8a32fd51e091ed5a35b0c699a52b
  GIT_SHALLOW FALSE
  SOURCE_SUBDIR host-source-only)
FetchContent_MakeAvailable(jpeg_header_guard_jpegdec)

set(JHG_PATCH_DIR ${REPO_ROOT}/scripts/jpegdec_patches)
set(JHG_GUARD_PATCH 0004-validate-jpeg-header-segments.patch)
file(GLOB JHG_PATCHES CONFIGURE_DEPENDS ${JHG_PATCH_DIR}/*.patch)
set(JHG_VARIANTS guarded guarded_observed control_observed)
option(JPEG_HEADER_GUARD_MUTANTS "Also build one decoder per disabled guard hunk (evidence only)" OFF)
if(JPEG_HEADER_GUARD_MUTANTS)
  foreach(hunk dht_id tiff_bounds marker_read segment_length segment_fit exif sof dqt sos
      tifflong dht_id_0_3 huff_codes sos_ids)
    list(APPEND JHG_VARIANTS mutant_${hunk})
  endforeach()
endif()

foreach(variant ${JHG_VARIANTS})
  set(dir ${CMAKE_CURRENT_BINARY_DIR}/${variant})
  add_custom_command(OUTPUT ${dir}/src/JPEGDEC.cpp ${dir}/src/jpeg.inl
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/make_variant.py
      ${jpeg_header_guard_jpegdec_SOURCE_DIR} ${JHG_PATCH_DIR} ${JHG_GUARD_PATCH} ${dir} ${variant}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/make_variant.py ${JHG_PATCHES} VERBATIM)
  add_executable(jpeg_header_guard_${variant} HeaderGuardTest.cpp ${dir}/src/JPEGDEC.cpp)
  set_property(SOURCE ${dir}/src/JPEGDEC.cpp APPEND PROPERTY OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ubsan-ignore.txt)
  target_include_directories(jpeg_header_guard_${variant} PRIVATE ${dir}/src ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(jpeg_header_guard_${variant} PRIVATE __LINUX__)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    # JPEGDEC's SSE marker filter compares with an uninitialised register and can leave restart markers in the
    # entropy data. No firmware target compiles that path; build the scalar code that the firmware compiles.
    target_compile_definitions(jpeg_header_guard_${variant} PRIVATE NO_SIMD)
  endif()
  target_compile_options(jpeg_header_guard_${variant} PRIVATE
    -O1 -g -fsanitize=address,undefined -fno-sanitize=alignment -fno-omit-frame-pointer
    -fsanitize-ignorelist=${CMAKE_CURRENT_SOURCE_DIR}/ubsan-ignore.txt)
  target_link_options(jpeg_header_guard_${variant} PRIVATE -fsanitize=address,undefined)
endforeach()

add_test(NAME JpegHeaderGuard COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run_cases.py
  --guarded $<TARGET_FILE:jpeg_header_guard_guarded>
  --guarded-observed $<TARGET_FILE:jpeg_header_guard_guarded_observed>
  --control $<TARGET_FILE:jpeg_header_guard_control_observed>
  --corpus ${REPO_ROOT}/test/image_golden/corpus.tar.gz ${REPO_ROOT}/test/image_golden/samples.tar.gz
  --report ${CMAKE_CURRENT_BINARY_DIR}/report.json)
