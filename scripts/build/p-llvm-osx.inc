# Package script for LLVM builds OSX
install_binary_artifact_llvm () {
  # Use the brew provided package
  LLVM_VERSION_MAJOR="${LLVM_VERSION/.*/}"
  brew install "llvm@${LLVM_VERSION_MAJOR}"
}

# Check if the binary artifact is installed
is_installed_llvm() {
    # Check if llvm-config with the right version exists
    LLVM_VERSION_MAJOR="${LLVM_VERSION/.*/}"
    [[ -f "/opt/homebrew/opt/llvm@${LLVM_VERSION_MAJOR}/bin/llvm-config" ]]
}

setup_artifact_variables_llvm() {
  LLVM_VERSION_MAJOR="${LLVM_VERSION/.*/}"
  # Consumed by the dynamically sourced build.sh component interface.
  # shellcheck disable=SC2034
  LLVM_CONFIG="/opt/homebrew/opt/llvm@${LLVM_VERSION_MAJOR}/bin/llvm-config"
  # Consumed by the dynamically sourced build.sh component interface.
  # shellcheck disable=SC2034
  BITCODE_CC="/opt/homebrew/opt/llvm@${LLVM_VERSION_MAJOR}/bin/clang"
  # Consumed by the dynamically sourced build.sh component interface.
  # shellcheck disable=SC2034
  BITCODE_CXX="/opt/homebrew/opt/llvm@${LLVM_VERSION_MAJOR}/bin/clang++"
  # Consumed by the dynamically sourced build.sh component interface.
  # shellcheck disable=SC2034
  LLVM_INSTALL="/opt/homebrew/opt/llvm@${LLVM_VERSION_MAJOR}/"
}
