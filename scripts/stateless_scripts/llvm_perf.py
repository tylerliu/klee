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
                    if file.endswith(".llvm_metrics"):
                        file_name = file.replace(
                            '.llvm_metrics', '')
                        for line in f:
                            line = line.rstrip()
                            op.write(file_name+","+line+"\n")


main()
