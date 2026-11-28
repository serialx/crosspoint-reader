add_executable(Utf8ComposeTest
  Utf8ComposeTest.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(Utf8ComposeTest PRIVATE
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(Utf8ComposeTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(Utf8ComposeTest)

add_executable(TextGoldenDump TextGoldenDump.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/Epub/Epub/CjkBreaks.cpp)
target_include_directories(TextGoldenDump PRIVATE ${REPO_ROOT}/lib/Utf8)
target_link_libraries(TextGoldenDump PRIVATE crosspoint_test_common)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME TextGoldens COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/text_goldens.py --dump $<TARGET_FILE:TextGoldenDump>)
