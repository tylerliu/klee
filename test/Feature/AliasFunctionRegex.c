// RUN: %clang %s -emit-llvm -O0 -c -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out %t1.bc > %t1.log
// RUN: grep -c foo %t1.log | grep 5
// RUN: grep -c bar %t1.log | grep 4

#include <klee/klee.h>

#include <stdio.h>
#include <stdlib.h>

void __attribute__ ((noinline)) fooXYZ() { printf("  foo()\n"); }
void __attribute__ ((noinline)) bar() { printf("  bar()\n"); }

int main() {
  int x;
  klee_make_symbolic(&x, sizeof(x), "x");

  // call once, so that it is not removed by optimizations
  bar();

  // no aliases
  fooXYZ();

  if (x > 10)
  {
    // foo -> bar
    klee_alias_function_regex("foo.*", "bar");

    if (x > 20)
      fooXYZ();
  }
  
  fooXYZ();

  // undo
  klee_alias_undo("foo.*");
  fooXYZ();
}
