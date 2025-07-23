# Script to analyze a sequence of memory accesses from an LLVM IR trace using a simple 1-level, set-associative LRU cache.
#
# $1: Input LLVM IR trace (lines with LOAD/STORE and addresses)
# $2: Output file, with total counts of hits and misses
#
# Note: This version does NOT use a common_stateless_cache_remnants file because
# address space randomization in LLVM IR traces makes such sharing unreliable.

import re
import sys

ip_file = sys.argv[1]  # LLVM IR trace
op_file = sys.argv[2]  # Output file with hit/miss counts

cache_size = 32768
cache_block_size = 64
set_associativity = 8
set_size = cache_block_size * set_associativity
num_sets = cache_size // set_size  # Integer division
cache_contents = [[] for _ in range(num_sets)]
cache_ages = [[] for _ in range(num_sets)]

# LRU replacement policy

load_re = re.compile(r"LOAD\s+(0x[0-9a-fA-F]+)")
store_re = re.compile(r"STORE\s+(0x[0-9a-fA-F]+)")


def main():
    global cache_contents
    global cache_ages
    hits = 0
    misses = 0
    with open(ip_file) as f:
        for line in f:
            text = line.rstrip()
            m = load_re.match(text)
            if not m:
                m = store_re.match(text)
            if m:
                addr_str = m.group(1)
                addr = int(addr_str, 16)
                block_no = addr // cache_block_size
                set_no = block_no % num_sets
                if block_no in cache_contents[set_no]:
                    # Hit: update LRU
                    hits += 1
                    update_ages(block_no, set_no)
                else:
                    # Miss: insert, evict if needed
                    misses += 1
                    if len(cache_contents[set_no]) >= set_associativity:
                        # Evict LRU
                        lru_index = cache_ages[set_no].index(max(cache_ages[set_no]))
                        del cache_contents[set_no][lru_index]
                        del cache_ages[set_no][lru_index]
                    cache_contents[set_no].append(block_no)
                    cache_ages[set_no].append(0)
                    update_ages(block_no, set_no)
    with open(op_file, "w") as output:
        output.write(f"Hits: {hits}\n")
        output.write(f"Misses: {misses}\n")

def update_ages(block_num, set_num):
    global cache_contents
    global cache_ages
    index = cache_contents[set_num].index(block_num)
    for x in range(len(cache_ages[set_num])):
        if x == index:
            cache_ages[set_num][x] = 0
        else:
            cache_ages[set_num][x] += 1

if __name__ == "__main__":
    main() 