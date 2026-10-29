find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME ui_runtime_cpp_goldens COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_oracle.py)
add_test(NAME ui_runtime_rust COMMAND cargo test --manifest-path ${CMAKE_CURRENT_SOURCE_DIR}/../../rust/Cargo.toml -p xp-ui)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/xp_ffi_host.cmake)
add_executable(ui_runtime_abi abi.cpp)
target_link_libraries(ui_runtime_abi PRIVATE xp_ffi_host)
target_compile_options(ui_runtime_abi PRIVATE -UNDEBUG)
add_test(NAME ui_runtime_abi COMMAND ui_runtime_abi)

# Compile the production adapter with deterministic input/display/task leaves.
set(ADAPTER_COPY ${CMAKE_CURRENT_BINARY_DIR}/adapter)
file(MAKE_DIRECTORY ${ADAPTER_COPY})
configure_file(${REPO_ROOT}/src/activities/RustActivityAdapter.h ${ADAPTER_COPY}/RustActivityAdapter.h COPYONLY)
configure_file(${REPO_ROOT}/src/activities/RustActivityAdapter.cpp ${ADAPTER_COPY}/RustActivityAdapter.cpp COPYONLY)
add_executable(ui_runtime_adapter adapter.cpp ${ADAPTER_COPY}/RustActivityAdapter.cpp)
target_include_directories(ui_runtime_adapter PRIVATE ${ADAPTER_COPY} stubs)
target_link_libraries(ui_runtime_adapter PRIVATE xp_ffi_host)
target_compile_options(ui_runtime_adapter PRIVATE -UNDEBUG)
add_test(NAME ui_runtime_adapter COMMAND ui_runtime_adapter)

add_test(NAME ui_runtime_rejection_families COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/rejection_families.py ${XP_FFI_HOST_ARCHIVE})
add_test(NAME ui_runtime_rejection_bypasses COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/rejection_bypasses.py ${XP_FFI_HOST_ARCHIVE})
add_test(NAME ui_runtime_rejection_composites COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/rejection_composites.py ${XP_FFI_HOST_ARCHIVE})
add_test(NAME ui_runtime_rejection_keyboard COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/rejection_keyboard.py ${XP_FFI_HOST_ARCHIVE})
add_test(NAME ui_runtime_rejection_commands COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/rejection_commands.py ${XP_FFI_HOST_ARCHIVE})
add_test(NAME ui_runtime_rejection_interleaving COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/rejection_interleaving.py ${XP_FFI_HOST_ARCHIVE} remap footnote clip-lock language text)
add_test(NAME ui_runtime_rejection_touch_recovery COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/rejection_interleaving.py ${XP_FFI_HOST_ARCHIVE} clip-touch clip-entry clip-entry-lock)

add_test(NAME ui_runtime_gate_adoption COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/gate_adoption.py ${XP_FFI_HOST_ARCHIVE})

add_test(NAME ui_runtime_stale_publication COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/stale_publication.py ${XP_FFI_HOST_ARCHIVE})

add_test(NAME ui_runtime_custom_gates COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/custom_gates.py ${XP_FFI_HOST_ARCHIVE})
