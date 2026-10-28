if(APPLE)
  set(SYNC_JSON_DIR "${REPO_ROOT}/.pio/libdeps/default/ArduinoJson/src" CACHE PATH "ArduinoJson headers for sync oracle")
  if(EXISTS "${SYNC_JSON_DIR}/ArduinoJson.h")
    add_executable(SyncOracle Oracle.cpp
      ${REPO_ROOT}/lib/KOReaderSync/ChapterXPathResolver.cpp
      ${REPO_ROOT}/lib/KOReaderSync/KOReaderCredentialStore.cpp
      ${REPO_ROOT}/lib/KOReaderSync/KOReaderDocumentId.cpp
      ${REPO_ROOT}/lib/KOReaderSync/KOReaderSyncClient.cpp
      ${REPO_ROOT}/lib/KOReaderSync/ProgressMapper.cpp
      ${REPO_ROOT}/lib/KOReaderSync/ProgressComparison.cpp
      ${REPO_ROOT}/lib/Utf8/Utf8.cpp
      ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
      ${REPO_ROOT}/lib/expat/ExpatErrors.cpp)
    target_include_directories(SyncOracle PRIVATE stubs ${SYNC_JSON_DIR}
      ${REPO_ROOT}/lib/KOReaderSync ${REPO_ROOT}/lib/Epub ${REPO_ROOT}/lib/Utf8
      ${REPO_ROOT}/lib/HttpHeader ${REPO_ROOT}/lib/expat)
    target_compile_options(SyncOracle PRIVATE -Wno-deprecated-declarations)
    target_link_libraries(SyncOracle PRIVATE crosspoint_test_common)
    add_test(NAME SyncGoldens COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/oracle.py $<TARGET_FILE:SyncOracle>)
  endif()
endif()
