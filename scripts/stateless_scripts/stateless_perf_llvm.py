# Script to calculate llvm metrics, for a given call_path
#
# $1: Input trace file (should be *.ll.tracelog.demarcated)
# $2: Output file for cache stats (memory access count)
# $3: Output file for LLVM metrics (instruction and memory instruction counts)
#
# Prints metrics to stdout as well as writes to the specified files.

import sys
import re
import os

input_trace = sys.argv[1]  # *.ll.tracelog.demarcated
cache_stats_file = sys.argv[2]
llvm_metrics_file = sys.argv[3]

# --- Original method (for original traces) ---
def original_metrics(demarcated_file):
    # Open the corresponding *.ll.demarcated file
    with open(demarcated_file, 'r') as trace_file:
        trace_lines = [x.rstrip() for x in trace_file]
    # Skip first two lines (header/convenience) if present
    instr_trace = trace_lines[2:] if len(trace_lines) > 2 and '|' in trace_lines[0] else trace_lines
    instructions = []
    for a in instr_trace:
        if a.count('|') == 2:
            instructions.append(a.split('|')[2].lstrip().rsplit())
    metric1 = len(instructions)
    metric2 = 0
    for i in instructions:
        if("alloca" in i):
            metric2 += 1
        elif("load" in i):
            metric2 += 1
        elif("store" in i):
            metric2 += 1
    print("[Original method]")
    print(f"llvm instruction count,{metric1}")
    print(f"llvm memory instructions,{metric2}")
    return metric1, metric2

# --- New method for demarcated files ---
def demarcated_metrics(cache_stats_file, demarcated_file):
    # Read the Hits and Misses from the cache stats file and sum them
    hits = 0
    misses = 0
    with open(cache_stats_file, 'r') as f:
        for line in f:
            if line.startswith("Hits:"):
                hits = int(line.strip().split(':')[1])
            elif line.startswith("Misses:"):
                misses = int(line.strip().split(':')[1])
    mem_accesses = hits + misses
    # Count number of instructions in the demarcated trace
    instr_count = 0
    with open(demarcated_file, 'r') as f:
        for line in f:
            l = line.strip()
            if not l:
                continue
            if l.startswith("LOAD ") or l.startswith("STORE "):
                continue
            if l.startswith("Call to ") or l.startswith("CALL "):
                continue
            if l == "Function | Instruction" or l == "EOF":
                continue
            instr_count += 1
    print("[Demarcated method]")
    print(f"Hits: {hits}")
    print(f"Misses: {misses}")
    print(f"llvm instruction count,{instr_count}")
    print(f"llvm memory instructions,{mem_accesses}")
    return hits, misses, mem_accesses, instr_count


def main():
    # Derive the corresponding *.ll.demarcated file
    if input_trace.endswith('.ll.tracelog.demarcated'):
        demarcated_file = input_trace.replace('.ll.tracelog.demarcated', '.ll.demarcated')
    else:
        raise ValueError("Input trace file must end with .ll.tracelog.demarcated")
    # Run both methods
    metric1, metric2 = original_metrics(demarcated_file)
    hits, misses, mem_accesses, instr_count = demarcated_metrics(cache_stats_file, input_trace)
    # Write to output files
    with open(cache_stats_file, 'w') as f:
        f.write(f"Hits: {hits}\n")
        f.write(f"Misses: {misses}\n")
    with open(llvm_metrics_file, 'w') as f:
        f.write(f"llvm instruction count,{instr_count}\n")
        f.write(f"llvm memory instructions,{mem_accesses}\n")

if __name__ == "__main__":
    main()

