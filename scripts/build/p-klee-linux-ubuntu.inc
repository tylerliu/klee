install_build_dependencies_klee() {
  apt update -y

  dependencies=(
    build-essential
    gcc-multilib # to generate 32bit
    cmake
    zlib1g-dev
    python3
    python3-setuptools
    python3-pip
    python3-wheel
    libtinfo-dev
  )

  if [[ $(to_bool "${COVERAGE}") -eq 1 ]]; then
    dependencies+=(lcov curl)
  fi
  apt -y --no-install-recommends install "${dependencies[@]}"

  pip3 install lit tabulate
}
