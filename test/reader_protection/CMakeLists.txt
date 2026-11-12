add_test(NAME reader_protection_parity
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run.py
    --build ${CMAKE_CURRENT_BINARY_DIR}/protection)
add_test(NAME reader_protection_reclaim_order
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/reclaim_order.py
    --build ${CMAKE_CURRENT_BINARY_DIR}/pressure)
