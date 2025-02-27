// RUN: %clang %s -emit-llvm -g -c -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --use-incomplete-merge --exit-on-error %t1.bc > %t.klee-out.log 2>&1 || echo fail >> %t.klee-out.log
// RUN: FileCheck %s < %t.klee-out.log

// CHECK: Loop analysis is not supported
// CHECK: fail

#include <klee/klee.h>
#include <stdio.h>

int main() {
  int x = 3;
  klee_possibly_havoc(&x, sizeof(x), "x");
  while(klee_induce_invariants() & --x) {
    printf("inloop\n");
  }
  printf("afterloop\n");
  return 0;
}
