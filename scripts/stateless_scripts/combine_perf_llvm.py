# Script to calculate performance of each trace, from computed parameters for llvm metrics
#
# $1: Input file extension with llvm instruction count, llvm mem accesses for each call_path
# $2 : Input file that contains x86 metrics for all call_paths
# $3: Output file, with performance value for each trace

import sys
import string
import os

extension = sys.argv[1]
perf_x86 = sys.argv[2]
perf_out = sys.argv[3]

with open(perf_out, 'w') as output:
    with open(perf_x86, 'r') as f1:
        
        x86_perf = [line for line in f1]
        current_id = x86_perf[0].split(',')[0]
        for perf in x86_perf:
            callpath_id = perf.split(',')[0]
            if(callpath_id == current_id):
                output.write(perf)
                continue
            else:
                # write llvm metrics for current_id to perf_out
                llvm_metric_file = current_id+"."+extension;
                with open(llvm_metric_file) as f2:
                    for metric in f2:
                        output.write(current_id+","+metric)

                # write metric of callpath_id to perf_out
                output.write(perf)
                
                # update current_id
                current_id = callpath_id

        # write llvm metrics for current_id (last) to perf_out
        llvm_metric_file = current_id+"."+extension;
        with open(llvm_metric_file) as f2:
            for metric in f2:
                output.write(current_id+","+metric)
