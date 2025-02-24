#!/bin/bash

# Master script to identify the precise execution path that an input takes through an NF.
# $1 - This is the NF
# $2 - This is the PCAP file that needs to be replayed


BOLT_DIR=~/projects/Bolt/bolt
KLEE_DIR=~/projects/Bolt/klee
REPLAY_SCRIPTS_DIR=$KLEE_DIR/scripts/replay-scripts
FN_LIST_DIR=$KLEE_DIR/scripts/stateless_scripts/fn_lists

NF=$1
PCAP=$2

pushd $BOLT_DIR/nf/testbed/hard >> /dev/null

  bash bench.sh $NF replay-pcap-instr "null" $PCAP
  pushd $NF >> /dev/null
    python3 $REPLAY_SCRIPTS_DIR/get-last-packet.py pincounts.log replay-trace
    python3 $REPLAY_SCRIPTS_DIR/demarcate-replay-trace.py replay-trace replay-demarcated $FN_LIST_DIR/stateful_fns.txt $FN_LIST_DIR/dpdk_fns.txt $FN_LIST_DIR/time_fns.txt
    python3 $REPLAY_SCRIPTS_DIR/cleanup-replay-trace.py replay-demarcated replay-branches

    bash ../test-bolt.sh $(basename $NF)
    pushd klee-last >> /dev/null
      python3 $REPLAY_SCRIPTS_DIR/match-branches.py ../replay-branches .
    popd >> /dev/null
  
  popd >> /dev/null

popd >> /dev/null