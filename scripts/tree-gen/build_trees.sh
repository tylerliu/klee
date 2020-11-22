#!/bin/bash

set -euo pipefail

KLEE_DIR=$KLEE_INCLUDE/..

#Defaults
MAX_PERF=-1
MIN_PERF=-1
TRACES_DIR=klee-last
CONSTRAINT_NODE=none

while getopts ":t:m:n:c:p" opt; do
  case $opt in
    t) TREE_TYPE="$OPTARG"
    ;;
    m) MAX_PERF="$OPTARG"
    ;;
    n) MIN_PERF="$OPTARG"
    ;;
    c) CONSTRAINT_NODE="$OPTARG"
    ;;
    p) TRACES_DIR="$OPTARG"
    ;;
    \?) echo "Invalid option -$OPTARG" >&2
    ;;
  esac
done

if [ $MIN_PERF -lt 0 ] || [ $MAX_PERF -lt 0 ]  ; then
  echo "Please set MAX_PERF and MIN_PERF"
  exit 0
fi

PORTLIST=$TRACES_DIR/../portlist
touch $PORTLIST
pushd $TRACES_DIR >> /dev/null

pushd ../ >> /dev/null
  rm -f res-tree* neg-tree*
popd >> /dev/null

rm -f tree*.dot 

if grep -q "TRAFFIC_CLASS" *.call_path; then
  grep "TRAFFIC_CLASS" *.call_path | awk -F: '{print $1 "," $2}' | awk -F' = ' '{print $1 "," $2}' | sed 's/\.call_path//g' > tc_tags
fi

touch tc_tags

TREE_FILE="constraint-tree.txt"
CONSTRAINT_FILE="constraint-branches.txt"

# METRICS=("instruction count" "memory instructions" "execution cycles")
METRICS=("llvm instruction count")
for METRIC in "${METRICS[@]}"; 
do 
  METRIC_NAME=$(echo "$METRIC" | sed -e 's/ /_/')
  python3 $KLEE_DIR/scripts/tree-gen/build_tree.py tc_tags combined_perf.txt perf-formula.txt "$METRIC" "neg-tree" $TREE_FILE $CONSTRAINT_FILE $MAX_PERF $MIN_PERF $CONSTRAINT_NODE
  python3 $KLEE_DIR/scripts/tree-gen/build_tree.py tc_tags combined_perf.txt perf-formula.txt "$METRIC" "res-tree" $TREE_FILE $CONSTRAINT_FILE $MAX_PERF $MIN_PERF $CONSTRAINT_NODE
done

mv res-tree* ../
mv neg-tree* ../

popd >> /dev/null

rm -f *tree*.png

# Generate visual representations
for DOT_FILE in $(ls $TRACES_DIR/../*tree*.dot); do
    TREE_FILE_NAME=${DOT_FILE/"dot"/"png"}
    TREE_FILE_NAME=${TREE_FILE_NAME##*/} ## Removes everything until last "/". Source - https://stackoverflow.com/questions/3162385/how-to-split-a-string-in-shell-and-get-the-last-field
    dot $DOT_FILE -T png -o $TREE_FILE_NAME
done

# Fix reused symbols
REUSED_SYMBOLS=$TRACES_DIR/reused-symbols.txt
FIXED_SYMBOLS=$TRACES_DIR/fixed-symbols.txt
bash $KLEE_DIR/scripts/gen-predictor/pre_processing.sh $REUSED_SYMBOLS $FIXED_SYMBOLS $PORTLIST >> /dev/null

for TREE_FILE in $(ls $TRACES_DIR/../*-tree*.txt); do
  python3 $KLEE_DIR/scripts/gen-predictor/fancy_replace.py $TREE_FILE $FIXED_SYMBOLS >> /dev/null
  bash $KLEE_DIR/scripts/gen-predictor/generate.sh $TREE_FILE >> /dev/null
  sed -i 's/\([a-z_.]*\)\(_in_\)\([^ :)]*\)/\3.contains(\1)/' $TREE_FILE.py
  sed -i 's/is_full/is_full()/' $TREE_FILE.py
done