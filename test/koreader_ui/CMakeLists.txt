find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME koreader_ui_original_effects COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_effects.py)
add_test(NAME koreader_ui_original_views COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_views.py)
