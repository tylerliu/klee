#!/bin/bash

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

REUSED_SYMBOLS=$1
FIXED_SYMBOLS=$2
PORT_LIST=$2

python3 $SCRIPT_DIR/gen_rules.py $PORT_LIST

pushd $SCRIPT_DIR >> /dev/null
  touch program_rules.ml
  make pre_processing.byte
popd >> /dev/null

cat $REUSED_SYMBOLS | cut -d "|" -f2 > temp
$SCRIPT_DIR/pre_processing.byte temp > temp1
cat $REUSED_SYMBOLS | cut -d "|" -f1 > temp
paste temp temp1 > $FIXED_SYMBOLS
rm temp temp1