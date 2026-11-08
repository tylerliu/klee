// RUN: %clang %s -emit-llvm %O0opt -c -o %t.bc
// RUN: rm -rf %t.default %t.logged
// RUN: %klee --output-dir=%t.default --exit-on-error %t.bc > %t.default.log 2>&1
// RUN: FileCheck --check-prefix=DEFAULT %s < %t.default.log
// RUN: %klee --output-dir=%t.logged --exit-on-error --debug-log-function-calls %t.bc > %t.logged.log 2>&1
// RUN: FileCheck --check-prefix=LOG %s < %t.logged.log

// DEFAULT-NOT: calling function
// DEFAULT: KLEE: done: completed paths = 1
// DEFAULT-NOT: calling function
// LOG-COUNT-1: KLEE: WARNING ONCE: calling function called_twice.
// LOG-NOT: calling function called_twice.
// LOG: KLEE: done: completed paths = 1
// LOG-NOT: calling function called_twice.

__attribute__((noinline)) void called_twice(void) {
  static volatile int count;
  ++count;
}
int main(void) {
  called_twice();
  called_twice();
  return 0;
}
