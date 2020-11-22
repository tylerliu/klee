
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
py_scripts_dir=$SCRIPT_DIR/stateless_scripts
TRACES_DIR=${1:-klee-last}
metrics=${2:-x86}

pushd $TRACES_DIR

python $py_scripts_dir/gen_formula.py $TRACES_DIR/stateful-formula.txt $TRACES_DIR/stateless-perf.txt $TRACES_DIR/perf-formula.txt $metrics

join -t, -j1 \
      <(sort $TRACES_DIR/stateful-perf.txt | awk -F, '{print $1 "_" $2 "," $3}') \
      <(sort $TRACES_DIR/stateless-perf.txt | awk -F, '{print $1 "_" $2 "," $3}') \
    | sed -e 's/_/,/' \
    | awk -F, '
      {
        if($3 >=0) {
          performance = ($3 + $4);
          print $1 "," $2 "," performance > "combined_perf.txt";
          if (performance > max_performance[$2]) {
            max_performance[$2] = performance;
            trace[$2] = $1;
          }
        }
      }

      END {
        for (metric in max_performance) {
          print metric "," max_performance[metric]" -  " trace[metric];
        }
      }'

popd