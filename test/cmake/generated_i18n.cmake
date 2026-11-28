# Script mode: publish the I18nKeys.h that a host archive was generated with.
#
#   cmake -DARCHIVE=<libxp_ffi.a> -DOUTPUT=<I18nKeys.h> -P generated_i18n.cmake
#
# Cargo writes the header into the per-crate output directory beside the archive.
# C++ code must include that copy: the numeric StrId values only match the
# archive when both come from the same generator run (P16-O5a-HF1).
if(NOT ARCHIVE OR NOT OUTPUT)
  message(FATAL_ERROR "generated_i18n.cmake needs -DARCHIVE=<libxp_ffi.a> and -DOUTPUT=<I18nKeys.h>")
endif()
get_filename_component(target_dir "${ARCHIVE}" DIRECTORY)
file(GLOB headers "${target_dir}/build/xp-core-*/out/I18nKeys.h")
if(NOT headers)
  message(FATAL_ERROR "no generated I18nKeys.h beside ${ARCHIVE}; expected ${target_dir}/build/xp-core-*/out/I18nKeys.h")
endif()
list(GET headers 0 first)
file(SHA256 "${first}" first_hash)
foreach(header ${headers})
  file(SHA256 "${header}" hash)
  if(NOT hash STREQUAL first_hash)
    message(FATAL_ERROR "conflicting generated I18nKeys.h beside ${ARCHIVE}: ${headers}")
  endif()
endforeach()
get_filename_component(output_dir "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")
configure_file("${first}" "${OUTPUT}" COPYONLY)
