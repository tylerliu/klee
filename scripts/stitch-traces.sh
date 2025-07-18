#!/bin/bash

set -euo pipefail

TRACES_DIR=${1:-klee-last}
shift || true

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PERF_CONTRACTS_PATH=$SCRIPT_DIR/../../pix/dpdk-nfs/perf-contracts/perf-contracts.so

function stitch_traces {
  if [ "${1}" ]; then
    eval "declare -A USER_VARS="${1#*=}
    
    USER_VAR_STR=""
    for VAR in "${!USER_VARS[@]}"; do
      USER_VAR_STR="$USER_VAR_STR,$VAR=(w32 ${USER_VARS[$VAR]})"
    done

    USER_VAR_STR="$(echo "$USER_VAR_STR" | sed -e 's/^,//')"

    touch $TRACES_DIR/stateful-error-log   
    rm $TRACES_DIR/stateful-error-log
    parallel --joblog joblog.txt -j$(nproc) --halt-on-error 0 "set -euo pipefail; $SCRIPT_DIR/../build/bin/stitch-perf-contract \
                  -contract $PERF_CONTRACTS_PATH \
                  --user-vars \"$USER_VAR_STR\" \
                  {} 2>> $TRACES_DIR/stateful-error-log-\$(basename {} .call_path) \
                | awk \"{ print \\\"\$(basename {} .call_path),\\\" \\\$0; }\"" \
                ::: $TRACES_DIR/*.call_path > $TRACES_DIR/stateful-analysis-log.txt
  else
    parallel --joblog joblog.txt -j$(nproc) --halt-on-error 0 "set -euo pipefail; $SCRIPT_DIR/../build/bin/stitch-perf-contract \
                  -contract $PERF_CONTRACTS_PATH \
                  {} 2>> $TRACES_DIR/stateful-error-log-$(basename {} .call_path) \
                | awk \"{ print \\\"\$(basename {} .call_path),\\\" \\\$0; }\"" \
                ::: $TRACES_DIR/*.call_path > $TRACES_DIR/stateful-analysis-log.txt
  fi
  
  if grep -q "Concrete State" stateful-analysis-log.txt; then
	  grep "Concrete State" stateful-analysis-log.txt > concrete-state-log.txt
  fi
  touch concrete-state-log.txt

  if grep -q "Perf Formula" stateful-analysis-log.txt; then
    grep "Perf Formula" stateful-analysis-log.txt | awk '{gsub(" Perf Formula:", "", $0); print}' > stateful-formula.txt
  fi
  touch stateful-formula.txt

  grep -v "Concrete State" stateful-analysis-log.txt | grep -v "Perf Formula" > stateful-perf.txt
  cat stateful-perf.txt | cut -d "," -f1 | sort -nr | uniq > relevant_traces
  #sed -i '1d' joblog.txt && cat joblog.txt | awk '{print $7}' | sort -nr | uniq -c  

}


declare -A USER_VARS=()

while [ "${1:-}" ]; do
  USER_VARS[$1]=$2
  shift 2
done

if [ ${#USER_VARS[@]} -gt 0 ]; then
  stitch_traces "$(declare -p USER_VARS)"

else
  stitch_traces ""
fi
