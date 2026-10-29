find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME ui_updates_original_effects
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/effects.py)
add_test(NAME ui_updates_original_widgets
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_widgets.py)
add_test(NAME ui_updates_original_percent
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_percent.py)
