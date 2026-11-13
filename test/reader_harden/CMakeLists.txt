add_test(NAME reader_harden
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
    --build ${CMAKE_CURRENT_BINARY_DIR}/harden)

add_test(NAME reader_harden_build_failure
  COMMAND ${CMAKE_COMMAND} -E env XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_build_failure.py)

add_test(NAME reader_harden_menu_failure
  COMMAND ${CMAKE_COMMAND} -E env XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_menu_failure.py)
