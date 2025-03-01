add_klee_unit_test(SolverTest
  SolverTest.cpp ../../lib/Core/Memory.cpp)
target_link_libraries(SolverTest PRIVATE kleeCore)

if (${ENABLE_Z3})
  add_klee_unit_test(Z3SolverTest
    Z3SolverTest.cpp ../../lib/Core/Memory.cpp)
target_link_libraries(Z3SolverTest PRIVATE kleeCore)
endif()
