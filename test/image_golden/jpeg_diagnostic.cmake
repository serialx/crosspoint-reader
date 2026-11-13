# Pattern initialization exposes unset dimensions in each independent control.
foreach(mode current old-control)
  set(diagnostic_source ${CMAKE_CURRENT_BINARY_DIR}/JpegDiagnostic-${mode}.cpp)
  set(control_arg)
  if(mode STREQUAL "old-control")
    set(control_arg old-control)
  endif()
  add_custom_command(OUTPUT ${diagnostic_source}
    COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/test/image_golden/jpeg_diagnostic_source.py
      ${REPO_ROOT}/lib/Epub/Epub/converters/JpegToFramebufferConverter.cpp ${diagnostic_source} ${control_arg}
    DEPENDS ${REPO_ROOT}/test/image_golden/jpeg_diagnostic_source.py ${REPO_ROOT}/lib/Epub/Epub/converters/JpegToFramebufferConverter.cpp VERBATIM)
  add_executable(jpeg_diagnostic_${mode} ${REPO_ROOT}/test/image_golden/JpegDiagnosticArguments.cpp ${diagnostic_source}
    ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
    ${JPEG_DIAGNOSTIC_DECODER})
  target_include_directories(jpeg_diagnostic_${mode} PRIVATE ${PNG_BENCH_INCLUDES})
  target_compile_definitions(jpeg_diagnostic_${mode} PRIVATE __LINUX__)
  target_compile_options(jpeg_diagnostic_${mode} PRIVATE -ftrivial-auto-var-init=pattern -UNDEBUG)
  target_link_libraries(jpeg_diagnostic_${mode} PRIVATE xp_ffi_host)
endforeach()
add_test(NAME JpegDiagnosticArguments COMMAND ${Python3_EXECUTABLE}
  ${REPO_ROOT}/test/image_golden/jpeg_diagnostic_arguments.py
  $<TARGET_FILE:jpeg_diagnostic_current> $<TARGET_FILE:jpeg_diagnostic_old-control>)
