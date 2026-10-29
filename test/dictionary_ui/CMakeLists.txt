find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME dictionary_ui_geometry COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/oracle.py)
add_test(NAME dictionary_ui_selection_pixels COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/paint_oracle.py)
