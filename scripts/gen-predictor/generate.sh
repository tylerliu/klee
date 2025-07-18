SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

CONSTRAINT_FILE=$1

if [[ $CONSTRAINT_FILE =~ .*res-tree.*$ ]]; then 
  EXPR_BUILDER=codegen.exe
else
  EXPR_BUILDER=neg_tree.exe
fi

pushd $SCRIPT_DIR >> /dev/null
  dune build $EXPR_BUILDER
popd >> /dev/null

if [[ $CONSTRAINT_FILE =~ .*res-tree.*$ ]]; then 
  $SCRIPT_DIR/_build/default/$EXPR_BUILDER $CONSTRAINT_FILE
else
  $SCRIPT_DIR/_build/default/$EXPR_BUILDER $CONSTRAINT_FILE > $CONSTRAINT_FILE.py
  python3 $SCRIPT_DIR/rewrite_neg_tree.py $CONSTRAINT_FILE.py
fi