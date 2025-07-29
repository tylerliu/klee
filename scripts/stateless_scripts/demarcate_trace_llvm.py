# Script to extract the portions of the LLVM IR executable trace that are part of the
# stateless code, hence removing the contents of modelled functions.
# $1: This is the input trace, containing IR instructions from both stateless
#     code and modelled stateful functions.
# $2: This is the output trace, which is the subset of the input trace.
#     All instructions from the stateless code is retained.
#     Instructions from stateful code is replaced with a one-line description
#     of the function call.
# $3: List of stateful functions that are modelled out during Symbex
# $4: List of framework functions modelled out. This is either DPDK functions
#     (if only the NF is being analyzed) and HW functions (if NF+DPDK is being analyzed)
# $5: List of time functions modelled out during Symbex
# $6: List of klee-specific functions that are called during Symbex but are
#     not part of the runtime NF executable.

import sys
import re
import string
import os

ip_trace = sys.argv[1]
op_trace = sys.argv[2]
stateful_file = sys.argv[3]
dpdk_file = sys.argv[4]
time_file = sys.argv[5]
verification_file = sys.argv[6]

def load_fn_list(filename):
    with open(filename, "r") as f:
        return [line.rstrip() for line in f if line.rstrip()]

def is_modelled_function(fn_name, stateful_fns, dpdk_fns, time_fns, verif_fns):
    if fn_name in stateful_fns:
        return 'libVig'
    if fn_name in dpdk_fns:
        return 'DPDK'
    if fn_name in time_fns:
        return 'Time'
    if fn_name in verif_fns or re.compile('klee*').match(fn_name) or re.compile('_exit@plt*').match(fn_name):
        return 'Verification'
    if fn_name.startswith("llvm.dbg."):
        return 'Debug'
    return None

def map_special_functions(fn_name):
    if fn_name == "dmap_get_value":
        return "klee_forbid_access"
    elif fn_name in ["map_put", "map_erase"]:
        return "klee_trace_extra_ptr"
    elif fn_name == "dchain_is_index_allocated":
        return "klee_int"
    elif fn_name in ["vector_borrow", "vector_return"]:
        return "ds_path_1"
    elif fn_name == "flood":
        return "flood"
    return fn_name

def parse_function_name_from_instruction(line):
    # Expects 'Function | Instruction | Operands' (Operands may be empty)
    parts = [p.strip() for p in line.split('|')]
    if len(parts) >= 3:
        return parts[0], parts[1]
    return None, None

def main():
    stateful_fns = load_fn_list(stateful_file)
    verif_fns = load_fn_list(verification_file)
    dpdk_fns = load_fn_list(dpdk_file)
    time_fns = load_fn_list(time_file)

    # Read all lines, strip newlines
    with open(ip_trace) as f:
        lines = [line.rstrip() for line in f if line.rstrip()]

    # Remove header and EOF
    lines = [l for l in lines if l != "Function | Instruction | Operands" and l != "EOF"]

    output_lines = []
    call_stack = []  # Stack of (function_name, model_type) tuples
    last_written_fn = None
    call_gap = 1
    i = 0
    n = len(lines)

    while i < n:
        text = lines[i]
        # Handle CALL line with lookahead logic
        if text.startswith("CALL "):
            call_match = re.match(r'CALL (\S+)\s*\(', text)
            if call_match:
                callee = call_match.group(1)
                model_type = is_modelled_function(callee, stateful_fns, dpdk_fns, time_fns, verif_fns)
                mapped_fn = map_special_functions(callee)
                # Find caller context (top of stack or None)
                caller = call_stack[-1][0] if call_stack else None
                # Look ahead: skip next line (should be 'function | call'), then check the following line
                next_i = i + 2
                # Skip over any CALL or LOAD/STORE lines
                while next_i < n and "|" not in lines[next_i]:
                    next_i += 1
                entered_callee = False
                if next_i < n:
                    fn_name, _ = parse_function_name_from_instruction(lines[next_i])
                    if fn_name == callee:
                        entered_callee = True
                # Write summary line for modelled function if needed
                if model_type:
                    if call_gap or last_written_fn != mapped_fn:
                        last_written_fn = mapped_fn
                        call_gap = 0
                        if model_type == 'libVig':
                            output_lines.append(f"Call to libVig model - {mapped_fn}")
                        elif model_type == 'DPDK':
                            output_lines.append(f"Call to DPDK model - {mapped_fn}")
                        elif model_type == 'Time':
                            output_lines.append(f"Call to Time model - {mapped_fn}")
                        elif model_type == 'Verification':
                            output_lines.append(f"Call to Verification Code - {mapped_fn}")
                    else:
                        # Back-to-back, skip
                        pass
                else:
                    # Not a modelled function, write the call
                    call_gap = 1
                    output_lines.append(text)
                # Only push callee if we actually enter it
                if entered_callee:
                    call_stack.append((callee, model_type))
                # Advance i: skip CALL, skip 'function | call' line
                i += 2
                continue
            else:
                # Couldn't parse, Throw error
                print(f"Error: Could not parse CALL line: {text}")
                exit(1)
        # LOAD/STORE line
        if text.startswith("LOAD ") or text.startswith("STORE "):
            # Only write if not in demarcated context
            if not (call_stack and call_stack[-1][1]):
                call_gap = 1
                output_lines.append(text)
            i += 1
            continue
        # Regular instruction line: 'Function | Instruction | Operands' (Operands may be empty)
        fn_name, instruction = parse_function_name_from_instruction(text)
        if fn_name is not None:
            # Rebuild call stack if function context changes
            while call_stack and call_stack[-1][0] != fn_name:
                call_stack.pop()
            # If this is a return, pop the stack
            if instruction == "ret" and call_stack and call_stack[-1][0] == fn_name:
                call_stack.pop()
                call_gap = 0
                i += 1
                continue  # Don't write ret for demarcated functions
            # Only write if not in demarcated context
            if not (call_stack and call_stack[-1][1]):
                call_gap = 1
                output_lines.append(text)
            i += 1
            continue
        # Malformed line, Throw error only if there are fewer than 3 fields
        print(f"Error: Malformed line: {text}")
        exit(1)

    with open(op_trace, "w") as output:
        for l in output_lines:
            output.write(l + "\n")

if __name__ == "__main__":
    main() 