# Script to calculate llvm metrics, for a given call_path
#
# $1: Input trace file
# $2: Output file, with performance value for each llvm metric

import sys
import os
import string
import re

input_trace = sys.argv[1]
output_perf = sys.argv[2]

with open(input_trace, 'r') as trace_file, open(output_perf, 'w') as perf_out:
    instr_trace = [x for x in trace_file]
    instr_trace = instr_trace[2:] # first two lines are for convenience

    instructions = []
    for a in instr_trace:
        if a.split('|').__len__() == 3:
            instructions.append(a.split('|')[2].lstrip().rsplit())

    # First we compute "llvm instruction count" for instr_trace
    metric1 = instructions.__len__()

    # Then we compute "llvm memory instructions" for instr_trace
    metric2 = 0
    for i in instructions:
        if("alloca" in i):
            metric2+=1
        elif("load" in i):
            metric2+=1
        elif("store" in i):
            metric2+=1

    perf_out.write("llvm instruction count,{}\n".format(metric1))
    perf_out.write("llvm memory instructions,{}\n".format(metric2))

