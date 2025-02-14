add_klee_unit_test(SolverTest
       SolverTest.cpp ../../lib/Core/Memory.cpp)
target_link_libraries(SolverTest PRIVATE kleeCore)
