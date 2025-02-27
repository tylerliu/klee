// RUN: %clang %s -emit-llvm -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --use-merge --debug-log-merge --search=bfs %t.bc 2>&1 | FileCheck %s
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --use-merge --debug-log-merge --search=dfs %t.bc 2>&1 | FileCheck %s
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --use-merge --debug-log-merge --search=nurs:covnew %t.bc 2>&1 | FileCheck %s

// CHECK: open merge:
// There will be 20 'close merge' statements. Only checking a few, the generated
// test count will confirm that the merge was closed correctly
// CHECK: close merge:
// CHECK: close merge:
// CHECK: close merge:
// CHECK: close merge:
// CHECK: close merge:
// CHECK: close merge:
// CHECK: close merge:
// CHECK: close merge:
// CHECK: close merge:
// CHECK: close merge:
// CHECK: close merge:
// CHECK: close merge:
// CHECK: close merge:
// CHECK: close merge:
// CHECK: close merge:
// CHECK: close merge:
// CHECK: close merge:
// CHECK: close merge:
// CHECK: close merge:
// CHECK: close merge:
// CHECK-NOT: close merge:

#include <klee/klee.h>
#include <stdio.h>


int main() {
  int x = 10;
  int z = 0;

  klee_possibly_havoc(&x, sizeof(x), "x");
  klee_possibly_havoc(&z, sizeof(z), "z");

  int n;
  klee_make_symbolic(&n, sizeof(n), "n");
  n = n % 20;

  klee_open_merge();
  for (int i = 0; i < n; i ++) {

    if (i == n - 1) {
      while(klee_induce_invariants() & --x) {
        if (z == 1) {
          printf("z may be 1\n");
          // CHECK-NOT: z may be 1
        } else {
          printf("z may be not 1\n");
          // CHECK: z may be not 1
        }
      }
      if (x == -1) printf("x may be -1\n");
      // CHECK-NOT: x may be -1

      klee_close_merge();
    }
  }

  if (n == 0) {
    klee_close_merge();
  }

  printf("\n ----- afterloop ---- \n");
  // CHECK: ----- afterloop ----
  return 0;
}
