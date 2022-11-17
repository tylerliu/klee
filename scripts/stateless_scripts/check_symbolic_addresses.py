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
stateful_fns_file = sys.argv[3]

cache_block_size = 64
addresses_touched = {}
symbolic_pcs = set()  # For debugging
common_cache_lines = set()
stateful_fns = list()


def main():

    with open(stateful_fns_file, "r") as stateful:
        stateful_fns = (line.rstrip() for line in stateful)
        stateful_fns = list(line for line in stateful_fns if line)

    for root, dirs, files in os.walk(trace_path):
        for file in files:
            with open(file) as f:
                if file.endswith(".packet.unclassified_mem_trace"):
                    for line in f:
                        text = line.rstrip()
                        if(":" in text):
                            text = text.rstrip().split(":")
                            pc = text[0]
                            addr = text[1]
                            if(pc in stateful_fns):
                                # We know these are common
                                common_cache_lines.add(addr)
                            else:
                                if(pc in addresses_touched):
                                    if(addr not in addresses_touched[pc]):
                                        addresses_touched[pc][addr] = 1
                                    else:
                                        addresses_touched[pc][addr] = addresses_touched[pc][addr]+1
                                else:
                                    addresses_touched[pc] = {}
                                    addresses_touched[pc][addr] = 1

    for pc, addresses in addresses_touched.items():
        if(len(addresses) == 1):
            common_cache_lines.update(set(addresses.keys()))
        else:
            occurences = set(addresses.values())
            common_addr = [
                key for(key, value) in addresses.items() if value == max(occurences)]
            # Pick the most common line(s) and add those
            common_cache_lines.update(set(common_addr))

    with open(op_file, "w") as op:
        for addr in common_cache_lines:
            addr = int(addr, 16)
            block_no = addr//cache_block_size
            op.write(str(hex(block_no)).upper()+"\n")


main()
