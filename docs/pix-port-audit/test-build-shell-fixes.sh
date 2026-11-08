#!/usr/bin/env bash
# Focused behavior tests; no package install, download, compiler or real build.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
build_dir=$root/scripts/build
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/llvm-config" <<'MOCK'
#!/usr/bin/env bash
case "$1" in
  --version) printf '%s\n' "${KLEE_TEST_VERSION:-18.1.3}" ;;
  --has-rtti) printf '%s\n' "${KLEE_TEST_RTTI:-YES}" ;;
  --assertion-mode) printf '%s\n' "${KLEE_TEST_ASSERTIONS:-ON}" ;;
  --shared-mode) [[ ${KLEE_TEST_SHARED_FAIL:-0} == 0 ]] || exit 1; echo shared ;;
  *) exit 1 ;;
esac
MOCK
chmod +x "$work/llvm-config"
to_bool() {
  case "$1" in
    1|ON|YES) echo 1;;
    0|OFF|NO) echo 0;;
    *) return 1;;
  esac
}
for distro in ubuntu debian arch; do
  (
    # shellcheck source=/dev/null
    source "$build_dir/p-llvm-linux-$distro.inc"
    export LLVM_VERSION=18.1 REQUIRES_RTTI=1 DISABLE_ASSERTIONS=0
    export KLEE_TEST_RTTI=NO
    check_llvm_config_version 0 "$work/llvm-config"
    if check_llvm_config_version 1 "$work/llvm-config"; then
      echo "FAIL: strict RTTI mismatch accepted ($distro)"; exit 1
    fi
    export KLEE_TEST_RTTI=YES
    check_llvm_config_version 1 "$work/llvm-config"
    export KLEE_TEST_ASSERTIONS=OFF
    if check_llvm_config_version 1 "$work/llvm-config"; then
      echo "FAIL: assertion mismatch accepted ($distro)"; exit 1
    fi
    export KLEE_TEST_ASSERTIONS=ON KLEE_TEST_SHARED_FAIL=1
    if check_llvm_config_version 1 "$work/llvm-config"; then
      echo "FAIL: failed shared-mode probe accepted ($distro)"; exit 1
    fi
    check_llvm_config_version 0 "$work/llvm-config"
    export KLEE_TEST_VERSION=17.0.1
    if check_llvm_config_version 0 "$work/llvm-config"; then
      echo "FAIL: wrong LLVM version accepted ($distro)"; exit 1
    fi
  )
done
printf 'PASS: relaxed/strict LLVM checks on Ubuntu, Debian and Arch\n'
for sanitizer in none memory; do
  (
    # shellcheck source=../../scripts/build/p-llvm.inc
    source "$build_dir/p-llvm.inc"
    export ENABLE_OPTIMIZED=1 ENABLE_DEBUG=0 DISABLE_ASSERTIONS=0 REQUIRES_RTTI=1
    export LLVM_INSTALL=$work/install SANITIZER_BUILD=$sanitizer LLVM_BUILD=$work/missing LLVM_BUILD_LIBCXX=$work/also-missing
    export LLVM_SRC_BASE=$work/source LLVM_VERSION_SHORT=181 SANITIZER_C_COMPILER=cc SANITIZER_CXX_COMPILER=c++
    mkdir() { :; } # Simulate a missing directory even after the mkdir step.
    cmake() { touch "$work/cmake-was-called"; }
    if configure_llvm 2>/dev/null; then
      echo 'FAIL: missing configure directory accepted'; exit 1
    fi
    [[ ! -e "$work/cmake-was-called" ]]
  )
done
(
  # shellcheck source=../../scripts/build/p-llvm.inc
  source "$build_dir/p-llvm.inc"
  export DIR=$build_dir ENABLE_OPTIMIZED=invalid ENABLE_DEBUG=0 DISABLE_ASSERTIONS=0 REQUIRES_RTTI=1
  to_bool() { touch "$work/boolean-was-checked"; return 1; }
  if setup_build_variables_llvm; then
    echo 'FAIL: invalid boolean configuration accepted'; exit 1
  fi
  [[ -e "$work/boolean-was-checked" ]]
)
printf 'PASS: failed cd and boolean conversion stop before build configuration\n'
