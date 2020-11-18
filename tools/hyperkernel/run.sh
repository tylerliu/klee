#!/bin/sh
# RUN THIS FILE FROM THE FOLDER THAT CONTAINS 'hyperkernel' AND 'klee' AS CREATED BY 'setup.sh'
# Args: the syscall signature in the form <return_type> <name> <arg0_type> <arg1_type> ...
# e.g. int sys_dup int int64_t int

CLANG_VER=8

./klee/tools/hyperkernel/gen-glue.py $@ | clang-$CLANG_VER -c -g -O0 -Xclang -disable-O0-optnone -emit-llvm -x c - -o x.bc
llvm-link-$CLANG_VER x.bc hyperkernel/hv6.bc -o full.bc
./klee/build/bin/klee -allocate-determ -allocate-determ-start-address=0x00040000000 -allocate-determ-size=1000 \
                      --external-calls=none --disable-verify -write-sym-paths -dump-call-traces -dump-call-trace-tree -dump-constraint-tree \
                      -solver-backend=z3 -exit-on-error -max-memory=750000 -search=dfs -condone-undeclared-havocs \
                      full.bc

