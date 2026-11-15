add_library(nothrow_allocation OBJECT ../../src/util/NothrowAllocation.cpp)
target_compile_definitions(nothrow_allocation PRIVATE malloc=nothrowTestMalloc)
add_executable(NothrowAllocationTest NothrowAllocationTest.cpp $<TARGET_OBJECTS:nothrow_allocation>)
add_test(NAME nothrow_allocation COMMAND NothrowAllocationTest)
