#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#
add_executable(ktest-dehavoc
  ktest-dehavoc.cpp
)

set(KLEE_LIBS
  kleeCore
)

target_link_libraries(ktest-dehavoc PRIVATE ${KLEE_LIBS})

install(TARGETS ktest-dehavoc RUNTIME DESTINATION bin)

llvm_config(ktest-dehavoc "${USE_LLVM_SHARED}" core support)
target_include_directories(ktest-dehavoc PRIVATE ${KLEE_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS} ${KLEE_SOURCE_DIR}/lib/Core)
target_compile_options(ktest-dehavoc PRIVATE ${KLEE_COMPONENT_CXX_FLAGS})
target_compile_definitions(ktest-dehavoc PRIVATE ${KLEE_COMPONENT_CXX_DEFINES})
