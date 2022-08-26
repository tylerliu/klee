# Script to check whether a line in the stateless code can access different memory locations
#
# $1: Directory with all traces
# $2: Output file with commonly used cache blocks
# $3:

import sys
import re
import string
import os

trace_path = sys.argv[1]
op_file = sys.argv[2]

cache_block_size = 64
common_cache_lines = set()


def main():

    for root, dirs, files in os.walk(trace_path):
        for file in files:
            with open(file) as f:
                if file.endswith(".packet.instruction.trace"):
                    for line in f:
                        pc = int(line.rstrip(),16)
                        block_no = pc//cache_block_size  # Integer division
                        common_cache_lines.add(pc)

    with open(op_file, "w") as op:
        for block in common_cache_lines:
            op.write(str(hex(block)).upper()+"\n")
        print("Total instruction footprint = %dB\n" %(len(common_cache_lines)*64))

main()
