#!/bin/sh
# RUN THIS FILE FROM YOUR HOME DIR (or somewhere else where you want to work)
# uses apt-get so ubuntu/debian-specific

CLANG_VER=8

# install clang and LLVM (g++ needed for clang++ to actually work)
sudo apt-get install -y clang-$CLANG_VER llvm-$CLANG_VER-dev g++-9

# install klee (and dependencies + Z3 as solver; note that zlib1g-dev is not in the official instructions :( also removed python cause python2 is dead anyway )
sudo apt-get install -y libz3-dev build-essential curl libcap-dev cmake libncurses5-dev unzip libtcmalloc-minimal4 libgoogle-perftools-dev libsqlite3-dev doxygen zlib1g-dev
if [ ! -d klee ]; then
  # I guess if you are running this script then you already have it? but maybe not in the right location so whatevs
  git clone --branch bolt-intrinsics https://github.com/bolt-perf-contracts/klee
fi
mkdir klee/build
cd klee/build
  cmake .. -DLLVM_CONFIG_BINARY="$(which llvm-config-$CLANG_VER)" -DENABLE_UNIT_TESTS=OFF -DENABLE_SYSTEM_TESTS=OFF -DENABLE_KLEE_ASSERTS=ON -DLLVMCC="$(which clang-$CLANG_VER)" -DLLVMCXX="$(which clang++-$CLANG_VER)"
  # not in public LLVM
  sed -i 's/obj->allocSite->getType()->dump();//' ../lib/Core/ExecutionState.cpp
  # not sure why cmake cannot figure out the need for -ldl ... adding it to CMAKE_CXX_FLAGS puts it too early in the line, so let's just patch
  sed -i 's/-o ../../bin/stitch-perf-contract/-o ../../bin/stitch-perf-contract -ldl' ./tools/stitch-perf-contract/CMakeFiles/stitch-perf-contract.dir/link.txt
  make -j $(nproc)
cd -

# install hyperkernel and patch it
git clone https://github.com/uw-unsat/hyperkernel
git -C hyperkernel apply "$(pwd)/klee/tools/hyperkernel/hyperkernel.patch"
# the hyperkernel requires 'clang' and 'llvm-link' by name
sudo ln -s /usr/bin/clang-$CLANG_VER /usr/bin/clang
sudo ln -s /usr/bin/llvm-link-$CLANG_VER /usr/bin/llvm-link
cd hyperkernel
  make o.x86_64/hv6/hv6.ll
  llvm-as-8 o.x86_64/hv6/hv6.ll -o hv6.bc
cd -
