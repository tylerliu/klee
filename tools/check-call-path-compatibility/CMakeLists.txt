#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#
add_executable(check-call-path-compatibility
  check-call-path-compatibility.cpp
)

set(KLEE_LIBS
  kleaverExpr
  kleeCore
)

set(LLVM_COMPONENTS
  support
)
klee_get_llvm_libs(LLVM_LIBS ${LLVM_COMPONENTS})

target_link_libraries(check-call-path-compatibility ${KLEE_LIBS} ${LLVM_LIBS})

install(TARGETS check-call-path-compatibility RUNTIME DESTINATION bin)
