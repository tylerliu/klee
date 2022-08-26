# Script to print concrete cache lines for instructions executed by the trace in the stateless code
# $1: This is the input trace, containing instructions from the stateless code.
# $2: Output file with list of addresses

import sys
import re
import string
import os

from collections import namedtuple

ip_trace = sys.argv[1]
op_trace = sys.argv[2]

# Assumptions
cache_line_size = 64


def main():

    with open(ip_trace) as f:
        if("Call to libVig model" in list(f)[-1]):
            print("Error in %s. LibVig call returned poorly" %
                  (ip_trace[0:10]))
            assert(0)
    f.close()

    with open(ip_trace,"r") as f, open(op_trace,"w") as op:
      for line in f:
          if(line.startswith(". DS Path")):
              print("DS Path inconsistency in %s" % (ip_trace))
              assert(0)
          elif(line.startswith("Call to")):
            continue 
          else:
              index = find_nth(line, "|", 1)
              pc = line[0:index].rstrip()
              op.write(pc+"\n")

def find_nth(haystack, needle, n):
    start = haystack.find(needle)
    while start >= 0 and n > 1:
        start = haystack.find(needle, start+len(needle))
        n -= 1
    return start


main()
