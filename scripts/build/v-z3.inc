# Build information for Z3 solver
required_variables_z3=(
  "Z3_VERSION"
)

# Artifacts Z3 depends on
# Consumed by the dynamically sourced build.sh component interface.
# shellcheck disable=SC2034
artifact_dependency_z3=("sanitizer")