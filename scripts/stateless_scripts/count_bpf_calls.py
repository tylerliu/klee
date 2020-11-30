# Script to put together data for all traces into a single file
#
# $1: Directory with all traces
# $2: Output file

import sys
import re
import string
import os

trace_path = sys.argv[1]
output = sys.argv[2]


def main():
    with open(output, "w") as op:
        for root, dirs, files in os.walk(trace_path):
            for file in files:
                with open(file) as f:
                    if file.endswith(".call_path"):
                        file_name = file.replace(
                            '.call_path', '')
                        lines = f.readlines()
                        num_calls = lines[len(lines)-1].rstrip()
                        op.write(file_name+","+num_calls+"\n")



main()