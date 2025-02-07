#!/bin/bash

# $1: "klee-only" to only build KLEE, or no argument to build everything
set -euo pipefail

KLEE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

[ -d "$KLEE_DIR/build" ] || mkdir "$KLEE_DIR/build"

pushd "$KLEE_DIR/build"

    [ -f "Makefile" ] || CXXFLAGS="-D_GLIBCXX_USE_CXX11_ABI=0" \
                        CMAKE_PREFIX_PATH="$KLEE_DIR/../z3/build" \
                        CMAKE_INCLUDE_PATH="$KLEE_DIR/../z3/build/include/" \
                        cmake \
                            -DENABLE_UNIT_TESTS=OFF \
                            -DBUILD_SHARED_LIBS=OFF \
                            -DLLVM_CONFIG_BINARY="$KLEE_DIR/../llvm/build/bin/llvm-config" \
                            -DLLVMCC="$KLEE_DIR/../llvm/build/bin/clang" \
                            -DLLVMCXX="$KLEE_DIR/../llvm/build/bin/clang++" \
                            -DENABLE_SOLVER_Z3=ON \
                            -DENABLE_KLEE_UCLIBC=ON \
                            -DKLEE_UCLIBC_PATH="$KLEE_DIR/../klee-uclibc" \
                            -DENABLE_POSIX_RUNTIME=ON \
                            -DCMAKE_BUILD_TYPE=Debug \
                            -DENABLE_KLEE_ASSERTS=ON \
                            -DENABLE_DOXYGEN=ON \
                            ..


    make -kj $(nproc)

popd

if [ $# -ne 0 ] && [ "$1" = "klee-only" ]; then
  echo "klee-only flag given, not installing other stuff"
  exit 0
fi

# Instruction Tracer
pushd trace-instructions
    make clean && make PINDIR="$KLEE_DIR/../pin"
popd

# Tree generation
sudo apt-get install python3
sudo apt-get install python3-pip
sudo python3 -m pip install anytree sympy delegator
opam install core
