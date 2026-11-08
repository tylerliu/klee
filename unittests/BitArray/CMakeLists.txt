add_klee_unit_test(BitArrayTest BitArrayTest.cpp)
target_compile_options(BitArrayTest PRIVATE ${KLEE_COMPONENT_CXX_FLAGS})
target_compile_definitions(BitArrayTest PRIVATE ${KLEE_COMPONENT_CXX_DEFINES})
target_include_directories(BitArrayTest PRIVATE ${KLEE_INCLUDE_DIRS})
