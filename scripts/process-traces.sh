#!/bin/bash

set -euo pipefail

TRACES_DIR=${1:-klee-last}
verif_arg=${2:-verify-dpdk}
metrics=${3:-x86}
latency_dir=${4:-}
shift 4 || true

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "Computing stateful bounds."
$SCRIPT_DIR/stitch-traces.sh $TRACES_DIR $@

echo "Computing stateless bounds."
$SCRIPT_DIR/stateless_perf.sh $TRACES_DIR stateless-perf.txt $verif_arg $metrics non-bpf "$latency_dir"

echo "Final touches"
$SCRIPT_DIR/combine.sh $TRACES_DIR $metrics
