#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

py_scripts_dir=$SCRIPT_DIR/stateless_scripts
traces_dir=${1:-klee-last} #traces_dir needs to contain files with the name *.packet_relevant_instructions 
output=${2:-stateless-perf.txt}
verif_arg=${3:-verify-dpdk}
metrics=${4:-x86}
bpf=${5:-non-bpf}

pushd $traces_dir

if [ "$verif_arg" == "verify-dpdk" ]; then
      stub_file=$py_scripts_dir/fn_lists/dpdk_fns.txt
else
      stub_file=$py_scripts_dir/fn_lists/hardware_fns.txt
fi

if [ $metrics == x86 ]; then
      echo Generating instruction traces

      parallel "grep \"|\" {} > \$(basename {} .tracelog).instructions" ::: *.tracelog

      echo Generating relevant instruction traces 

      parallel "$py_scripts_dir/process_trace.sh {} \$(basename {} .instructions).packet_relevant_instructions \$(basename {} .instructions).tracelog \$(basename {} .instructions).packet_relevant_tracelog $verif_arg" ::: *.instructions

      echo Generating demarcated instruction traces 

      parallel "python3 $py_scripts_dir/demarcate_trace.py {} \
                        \$(basename {} .packet_relevant_instructions).packet.demarcated \
                        \$(basename {} .packet_relevant_instructions).packet_relevant_tracelog \
                        \$(basename {} .packet_relevant_instructions).tracelog.demarcated \
                        $py_scripts_dir/fn_lists/stateful_fns.txt \
                        $stub_file \
                        $py_scripts_dir/fn_lists/time_fns.txt \
                        $py_scripts_dir/fn_lists/verif_fns.txt" \
                        ::: *.packet_relevant_instructions 

      echo Cleaning up instruction traces to allow path comparison

      parallel "python3 $py_scripts_dir/cleanup-instr-trace.py {} \
                        \$(basename {} .packet.demarcated).packet.comparison.trace" \
                        ::: *.packet.demarcated 

      echo Generating instruction trace so that we can analyze i-cache footprint

      parallel "python3 $py_scripts_dir/extract-instr-trace.py {} \
                        \$(basename {} .packet.demarcated).packet.instruction.trace" \
                        ::: *.packet.demarcated 
      python3 $py_scripts_dir/instr-footprint.py ./ insns-footprint.txt

      echo Generating address traces

      touch concrete-state-log.txt # The stateful code should do this, but this is in case one wants to use only the stateless code (e.g., hyperkernel)
      parallel "python3 $py_scripts_dir/print_addresses.py {} \
                        \$(basename {} .packet.demarcated).tracelog.demarcated \
                        concrete-state-log.txt \
                        \$(basename {} .packet.demarcated).packet.unclassified_mem_trace \
                        \$(basename {} .packet.demarcated).packet.duplicated" \
                        ::: *.packet.demarcated

      echo Checking new hypothesis
      touch common_stateless_cache_remnants
      # TODO: Fix this - address space randomization makes every address seen once; so everything is a hit. 
      python3 $py_scripts_dir/check_symbolic_addresses.py ./ \
                        common_stateless_cache_remnants \
                        $py_scripts_dir/fn_lists/stateful_fns.txt

      echo Classifying address traces 

      parallel "python $py_scripts_dir/formal_cache.py {} \
                        \$(basename {} .packet.unclassified_mem_trace).packet.classified_mem_trace \
                        common_stateless_cache_remnants" \
                        ::: *.packet.unclassified_mem_trace

      echo Putting it together 
      python3 $py_scripts_dir/stateless_stats.py ./ comp_insns num_accesses num_hits num_misses trace_nos
      python3 $py_scripts_dir/stateless_perf.py  comp_insns num_accesses num_hits num_misses trace_nos $output 

      rm -f $traces_dir/*.packet.stateless_mem_trace \
      $traces_dir/*.packet.stateless_mem_trace.classified
else
      echo "Generating LLVM metrics"

      echo Generating demarcated instruction traces 

      parallel "python3 $py_scripts_dir/demarcate_trace_llvm.py {} \
                        {}.demarcated \
                        $py_scripts_dir/fn_lists/stateful_fns.txt \
                        $stub_file \
                        $py_scripts_dir/fn_lists/time_fns.txt \
                        $py_scripts_dir/fn_lists/verif_fns.txt" ::: *.ll.tracelog  
      
      echo Classifying address traces 

      parallel "python $py_scripts_dir/formal_cache_llvm.py {} \
                        \$(basename {} .ll.tracelog.demarcated).ll.cache_stats" \
                        ::: *.ll.tracelog.demarcated

      #traces_dir needs to contain files with the name *.ll.demarcated
      parallel "python3 $py_scripts_dir/stateless_perf_llvm.py {} \
                        \$(basename {} .tracelog.demarcated).cache_stats \
                        \$(basename {} .ll.tracelog.demarcated).llvm_metrics" \
                        ::: *.ll.tracelog.demarcated

      python3 $py_scripts_dir/llvm_perf.py ./ $output

      # #Combine llvm metrics with x86_metrics
      # python3 $py_scripts_dir/combine_perf_llvm.py llvm_metrics x86_metrics $output
fi 

## Setup bpf-calls file independent of metric
python3 $py_scripts_dir/count_bpf_calls.py ./ bpf-calls.txt

popd