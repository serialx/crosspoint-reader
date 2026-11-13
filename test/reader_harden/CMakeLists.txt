add_test(NAME reader_harden
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
    --build ${CMAKE_CURRENT_BINARY_DIR}/harden)

add_test(NAME reader_harden_build_failure
  COMMAND ${CMAKE_COMMAND} -E env XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_build_failure.py)

add_test(NAME reader_harden_menu_failure
  COMMAND ${CMAKE_COMMAND} -E env XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_menu_failure.py)

add_test(NAME reader_harden_sync_metadata_failure
  COMMAND ${CMAKE_COMMAND} -E env XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_sync_metadata_failure.py)

add_test(NAME reader_harden_chapter_recovery
  COMMAND ${CMAKE_COMMAND} -E env XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_chapter_recovery.py)

add_test(NAME reader_harden_forced_break
  COMMAND ${CMAKE_COMMAND} -E env XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_forced_break.py)

add_test(NAME reader_harden_cosmetic_failure
  COMMAND ${CMAKE_COMMAND} -E env XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_cosmetic_failure.py)

add_test(NAME reader_status_unknown_progress
  COMMAND ${CMAKE_COMMAND} -E env XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_status_failure.py)

add_test(NAME reader_harden_recovery_input
  COMMAND ${CMAKE_COMMAND} -E env XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_recovery_input.py)

add_test(NAME reader_harden_recovery_back
  COMMAND ${CMAKE_COMMAND} -E env XP_TEST_RUST_ARCHIVE=${XP_FFI_HOST_ARCHIVE}
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_recovery_back.py)
