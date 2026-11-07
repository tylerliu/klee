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

target_link_libraries(check-call-path-compatibility PRIVATE ${KLEE_LIBS})

install(TARGETS check-call-path-compatibility RUNTIME DESTINATION bin)

llvm_config(check-call-path-compatibility "${USE_LLVM_SHARED}" core support)
target_include_directories(check-call-path-compatibility PRIVATE ${KLEE_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS} ${KLEE_SOURCE_DIR}/lib/Core)
target_compile_options(check-call-path-compatibility PRIVATE ${KLEE_COMPONENT_CXX_FLAGS})
target_compile_definitions(check-call-path-compatibility PRIVATE ${KLEE_COMPONENT_CXX_DEFINES})
