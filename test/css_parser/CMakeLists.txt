find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GLOB_RECURSE CSS_GOLDEN_INPUTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/goldens/*")
set(CSS_GOLDEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/goldens")
add_custom_command(
  OUTPUT "${CSS_GOLDEN_DIR}/unpacked.stamp"
  COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/unpack_goldens.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/goldens" "${CSS_GOLDEN_DIR}"
  COMMAND ${CMAKE_COMMAND} -E touch "${CSS_GOLDEN_DIR}/unpacked.stamp"
  DEPENDS ${CSS_GOLDEN_INPUTS} "${CMAKE_CURRENT_SOURCE_DIR}/unpack_goldens.py"
  VERBATIM
)
add_custom_target(css_goldens DEPENDS "${CSS_GOLDEN_DIR}/unpacked.stamp")

add_executable(CssParserTest
  CssParserTest.cpp
  CssGoldenTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp
)

target_include_directories(CssParserTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/Epub/Epub/css
  ${REPO_ROOT}/lib/AsciiText
  ${REPO_ROOT}/lib/Memory
)

target_link_libraries(CssParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

add_dependencies(CssParserTest css_goldens)
target_compile_definitions(CssParserTest PRIVATE CSS_GOLDEN_DIR="${CSS_GOLDEN_DIR}")
gtest_discover_tests(CssParserTest)
