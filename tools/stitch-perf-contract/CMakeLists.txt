#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#
add_executable(stitch-perf-contract
  stitch-perf-contract.cpp
)

set(KLEE_LIBS
  kleaverExpr
  kleeCore
)

target_link_libraries(stitch-perf-contract PRIVATE ${KLEE_LIBS})
target_link_libraries(stitch-perf-contract PRIVATE dl)

install(TARGETS stitch-perf-contract RUNTIME DESTINATION bin)

llvm_config(stitch-perf-contract "${USE_LLVM_SHARED}" core support)
target_include_directories(stitch-perf-contract PRIVATE ${KLEE_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS} ${KLEE_SOURCE_DIR}/lib/Core)
target_compile_options(stitch-perf-contract PRIVATE ${KLEE_COMPONENT_CXX_FLAGS})
target_compile_definitions(stitch-perf-contract PRIVATE ${KLEE_COMPONENT_CXX_DEFINES})
