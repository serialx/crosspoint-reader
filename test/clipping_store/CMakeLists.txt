find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(CLIPPING_FROZEN ${CMAKE_CURRENT_BINARY_DIR}/native)
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../clipping_bytes/prepare_native.py
  ${CLIPPING_FROZEN} COMMAND_ERROR_IS_FATAL ANY)
foreach(target ClippingStoreTest ClippingStoreS3Test)
  add_executable(${target} ClippingStoreTest.cpp ${CLIPPING_FROZEN}/src/ClippingStore.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp)
  target_link_libraries(${target} PRIVATE crosspoint_test_common GTest::gtest_main)
  target_include_directories(${target} PRIVATE stubs ../library_builder/stubs ${CLIPPING_FROZEN}/src ${REPO_ROOT}/src ${REPO_ROOT}/lib/Serialization ${REPO_ROOT}/lib/Utf8)
  gtest_discover_tests(${target} TEST_PREFIX "${target}.")
endforeach()
target_compile_definitions(ClippingStoreS3Test PRIVATE CONFIG_IDF_TARGET_ESP32S3=1)

add_test(NAME clipping_owner_rust
  COMMAND ${XP_FFI_CARGO} test --offline --locked
    --manifest-path ${CMAKE_CURRENT_SOURCE_DIR}/rust/Cargo.toml
    --target-dir ${CMAKE_CURRENT_BINARY_DIR}/rust-target)

add_executable(ClippingReadFolderPhysical ReadFolderPhysical.cpp)
target_compile_options(ClippingReadFolderPhysical PRIVATE -UNDEBUG)
target_link_libraries(ClippingReadFolderPhysical PRIVATE crosspoint_test_common)
target_include_directories(ClippingReadFolderPhysical PRIVATE ${REPO_ROOT}/test ${REPO_ROOT}/test/reader_lifecycle)
foreach(case success clip book key rights cache collision other-state)
  add_test(NAME clipping_read_folder_physical_${case} COMMAND ClippingReadFolderPhysical ${case})
endforeach()

add_executable(ClippingAppStorePhysical AppStorePhysical.cpp)
target_compile_options(ClippingAppStorePhysical PRIVATE -UNDEBUG)
target_link_libraries(ClippingAppStorePhysical PRIVATE crosspoint_test_common)
target_include_directories(ClippingAppStorePhysical PRIVATE ${REPO_ROOT}/test ${REPO_ROOT}/test/reader_lifecycle)
add_test(NAME clipping_app_store_physical COMMAND ClippingAppStorePhysical)

add_executable(ClippingFacadePhysical FacadePhysical.cpp
  ${REPO_ROOT}/src/ClippingStore.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp)
target_compile_options(ClippingFacadePhysical PRIVATE -UNDEBUG)
target_compile_definitions(ClippingFacadePhysical PRIVATE XP_FFI_HOST_FAULT_INJECTION CONFIG_IDF_TARGET_ESP32C3=1)
target_link_libraries(ClippingFacadePhysical PRIVATE crosspoint_test_common)
target_include_directories(ClippingFacadePhysical PRIVATE
  ../clipping_sync/stubs stubs ../library_builder/stubs
  ${REPO_ROOT}/test ${REPO_ROOT}/test/reader_lifecycle
  ${REPO_ROOT}/src ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/Serialization ${REPO_ROOT}/lib/Utf8)
add_test(NAME clipping_facade_physical COMMAND ClippingFacadePhysical)
