#!/usr/bin/env python3
import sys
print('#include <stddef.h>')
print('#include <stdint.h>')
print()
print('void hv6_main(void);')
print()
print('void klee_make_symbolic(void *addr, size_t nbytes, const char *name);')
print()
print(f'{sys.argv[1]} {sys.argv[2]}({", ".join(sys.argv[3:])});')
print()
print('int main(int argc, char** argv) {')
print('\thv6_main();')
print()
if len(sys.argv) > 3:
  for idx, arg in enumerate(sys.argv[3:]):
    print(f'\t{arg} arg{idx};')
    print(f'\tklee_make_symbolic(&arg{idx}, sizeof({arg}), "arg{idx}");')
print()
print(f'\t{sys.argv[2]}' + (f'({", ".join(map(lambda idx: f"arg{idx}", range(len(sys.argv)-3)))});' if len(sys.argv) > 3 else '();'))
print()
print('\treturn 0;')
print('}')
