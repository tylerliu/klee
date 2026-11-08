#!/usr/bin/env bash
set -euo pipefail
llvm_root=$1
expect_failure=$2
"$llvm_root/bin/llvm-config" --version
g++ --version | head -1
config=$(mktemp -d)
trap 'rm -rf "$config"' EXIT
cp -r /config/include "$config/"
version=$("$llvm_root/bin/llvm-config" --version)
major=${version%%.*}
minor=${version#*.}; minor=${minor%%.*}
sed -i -e "s/^#define LLVM_VERSION_MAJOR .*/#define LLVM_VERSION_MAJOR $major/" \
       -e "s/^#define LLVM_VERSION_MINOR .*/#define LLVM_VERSION_MINOR $minor/" \
       "$config/include/klee/Config/config.h"
flags=(-I/src/include -I"$config/include" -I/src/lib/Module -I"$llvm_root/include" -std=gnu++17
       -D_GNU_SOURCE -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS
       -fno-exceptions -fsyntax-only)
if g++ "${flags[@]}" /config/before-KModule.cpp > /tmp/before-module.log 2>&1; then
    [[ $expect_failure == no ]]
    echo 'Baseline compiles with this header set (transitive include remains available)'
else
    grep 'incomplete type' /tmp/before-module.log
    echo 'PASS: reproduced the reported baseline dominator type error'
fi
g++ "${flags[@]}" /src/lib/Module/KModule.cpp
echo 'PASS: fixed KModule compiles'
if g++ "${flags[@]}" -Werror=format /config/before-LoopAnalysis.cpp > /tmp/before-loop.log 2>&1; then
    echo 'FAIL: baseline format check unexpectedly passed'; exit 1
fi
grep 'format' /tmp/before-loop.log | head -2
g++ "${flags[@]}" -Werror=format /src/lib/Module/LoopAnalysis.cpp
echo 'PASS: fixed loop diagnostics compile with -Werror=format'
