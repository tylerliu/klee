local_setup_build_variables_clang() {
  source "${DIR}/common-functions"
  LLVM_SUFFIX=""

  LLVM_SUFFIX+="_O"
  LLVM_SUFFIX+="_ND"
  LLVM_SUFFIX+="_NA"
  LLVM_SUFFIX+="_RTTI"

  LLVM_SRC_BASE="${BASE}/llvm-${LLVM_VERSION_SHORT}"
  LLVM_INSTALL="${LLVM_SRC_BASE}-install${LLVM_SUFFIX}"
  LLVM_BIN="${LLVM_INSTALL}/bin"

  # Consumed by the dynamically sourced build.sh component interface.
  # shellcheck disable=SC2034
  LLVM_CONFIG="${LLVM_BIN}/llvm-config"
  # Consumed by the dynamically sourced build.sh component interface.
  # shellcheck disable=SC2034
  BITCODE_CC="${LLVM_BIN}/clang"
  # Consumed by the dynamically sourced build.sh component interface.
  # shellcheck disable=SC2034
  BITCODE_CXX="${LLVM_BIN}/clang++"
}

get_docker_config_id_clang() {
  (
    local_setup_build_variables_clang
    echo "${LLVM_VERSION_SHORT}${LLVM_SUFFIX}"
  )
}

get_build_artifacts_clang() {
  (
    local_setup_build_variables_clang
    echo "${LLVM_INSTALL}"
  )
}

# Check if the binary artifact is installed
is_installed_clang() {

  (
    local_setup_build_variables_clang
    # Check if clang is installed at a potential location
    [[ -f "${LLVM_BIN}/clang" ]]
  )
}

setup_artifact_variables_clang() {
  is_installed_clang || return 1

  local_setup_build_variables_clang
  echo "${LLVM_INSTALL}"
}

install_build_dependencies_clang() {
  return 0
}

setup_build_variables_clang() {
  return 0
}