# Script to calculate llvm metrics, for a given call_path
#
# $1: Input trace file (should be *.ll.tracelog.demarcated)
# $2: Output file for cache stats (memory access count)
# $3: Output file for LLVM metrics (instruction and memory instruction counts)
# $4: Latency data directory (contains ir-perf CSV and api-perf JSON files)
#
# Prints metrics to stdout as well as writes to the specified files.

import sys
import re
import os
import json
import csv

input_trace = sys.argv[1]  # *.ll.tracelog.demarcated
cache_stats_file = sys.argv[2]
llvm_metrics_file = sys.argv[3]
latency_dir = sys.argv[4] if len(sys.argv) > 4 else None

# --- Latency data parsing functions ---
def parse_ir_perf_csv(latency_dir):
    """Parse ir-perf CSV file to get instruction type -> latency mapping"""
    ir_perf_file = os.path.join(latency_dir, "ir-perf", "latency_summary_*.csv")
    import glob
    csv_files = glob.glob(ir_perf_file)
    if not csv_files:
        return {}
    
    # Use the most recent file
    latest_csv = max(csv_files, key=os.path.getctime)
    
    instruction_latencies = {}
    with open(latest_csv, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            instruction_latencies[row['instruction_type']] = float(row['latency'])
    
    return instruction_latencies

def parse_api_perf_json(latency_dir):
    """Parse api-perf JSON file to get function name -> latency mapping"""
    api_perf_file = os.path.join(latency_dir, "api-perf", "function_latency_map.json")
    if not os.path.exists(api_perf_file):
        return {}
    
    with open(api_perf_file, 'r') as f:
        data = json.load(f)
    
    function_latencies = {}
    for func_name, func_data in data.items():
        if 'base_latency_cycles' in func_data:
            function_latencies[func_name] = func_data['base_latency_cycles']
    
    return function_latencies


# --- Cache statistics parsing ---
def parse_cache_stats(cache_stats_file):
    """Parse cache hit/miss statistics from file"""
    load_hits = 0
    load_misses = 0
    store_hits = 0
    store_misses = 0
    
    with open(cache_stats_file, 'r') as f:
        for line in f:
            line = line.strip()
            if line.startswith("Load_Hits:"):
                load_hits = int(line.split(':')[1])
            elif line.startswith("Load_Misses:"):
                load_misses = int(line.split(':')[1])
            elif line.startswith("Store_Hits:"):
                store_hits = int(line.split(':')[1])
            elif line.startswith("Store_Misses:"):
                store_misses = int(line.split(':')[1])
            # Fallback for old format
            elif line.startswith("Hits:"):
                load_hits = int(line.split(':')[1])
                store_hits = 0
            elif line.startswith("Misses:"):
                load_misses = int(line.split(':')[1])
                store_misses = 0
    
    return load_hits, load_misses, store_hits, store_misses

# --- Memory latency parsing ---
def parse_memory_latency_csv(latency_dir):
    """Parse memory latency data from CSV file"""
    memory_latency_file = os.path.join(latency_dir, "ir-perf", "memory_latency_analysis_*.csv")
    import glob
    csv_files = glob.glob(memory_latency_file)
    if not csv_files:
        return {}
    
    # Use the most recent file
    latest_csv = max(csv_files, key=os.path.getctime)
    
    memory_latencies = {}
    with open(latest_csv, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            operation = row['operation']
            cache_level = row['cache_level']
            latency = float(row['latency_cycles'])
            memory_latencies[f"{operation}_{cache_level}"] = latency
    
    return memory_latencies

def calculate_memory_latency(load_hits, load_misses, store_hits, store_misses, memory_latencies):
    """Calculate total memory latency based on cache performance and parsed data"""
    # Use parsed memory latency data for both load and store operations
    load_l1_latency = memory_latencies.get('load_L1', 1.0)  # Default fallback
    load_l2_latency = memory_latencies.get('load_L2', 10.0)  # Default fallback
    store_l1_latency = memory_latencies.get('store_L1', 1.0)  # Default fallback
    store_l2_latency = memory_latencies.get('store_L2', 10.0)  # Default fallback
    
    load_latency = (load_hits * load_l1_latency) + (load_misses * load_l2_latency)
    store_latency = (store_hits * store_l1_latency) + (store_misses * store_l2_latency)
    
    return load_latency + store_latency

# --- Instruction latency calculation ---
def calculate_instruction_latency(instruction, operands, instruction_latencies, call_latency_table):
    """Calculate latency for a single instruction based on its type and operands"""
    # Special cases: ret and phi have no overhead
    if instruction.startswith('ret') or instruction.startswith('phi'):
        return 0.0
    
    # Handle call instructions (not to special functions)
    if instruction.startswith('call'):
        # Use interpolated latency based on known samples (void/1/2/4 args)
        arg_count = operands.count(',') + 1 if operands else 0
        return interpolate_call_latency(arg_count, call_latency_table, instruction_latencies)
    
    # Handle branch instructions - differentiate between jump and conditional branch
    if instruction.startswith('br'):
        operand_count = operands.count(',') + 1 if operands else 0
        base_instruction = instruction.split()[0] if ' ' in instruction else instruction
        
        if operand_count == 1:
            # Unconditional jump: just a label (direct jump)
            for instr_type, latency in instruction_latencies.items():
                if 'jump-direct' in instr_type:
                    return latency
            return 1.0  # Fallback
        elif operand_count == 3:
            # Conditional branch: condition (reg), label1, label2 (alternating)
            for instr_type, latency in instruction_latencies.items():
                if 'alternating' in instr_type:
                    return latency
            return 2.0  # Fallback
        else:
            # throw error
            raise ValueError(f"Invalid operand count for branch instruction: {operand_count}")
    
    # Handle memory instructions - use cache hit/miss latencies
    if instruction.startswith('load') or instruction.startswith('store'):
        return 0.0  # Will be handled by memory latency calculation
    
    # Handle two-operand instructions: use operand count from trace
    operand_count = operands.count(',') + 1 if operands else 0
    
    if operand_count == 2:
        return calculate_two_operand_latency(instruction, operands, instruction_latencies)
    
    # For other instructions, try to match with latency data
    return calculate_generic_instruction_latency(instruction, instruction_latencies)

def calculate_two_operand_latency(instruction, operands, instruction_latencies):
    """Calculate latency for two-operand instructions (reg vs imm)"""
    has_constant = any(char.isdigit() for char in operands)
    base_instruction = instruction.split()[0] if ' ' in instruction else instruction
    
    # Map to latency based on operand types
    if has_constant:
        # Look for -imm version first, fallback to -reg
        for instr_type, latency in instruction_latencies.items():
            if base_instruction in instr_type and '-imm' in instr_type:
                return latency
        for instr_type, latency in instruction_latencies.items():
            if base_instruction in instr_type and '-reg' in instr_type:
                return latency
    else:
        # Look for -reg version first, fallback to -imm
        for instr_type, latency in instruction_latencies.items():
            if base_instruction in instr_type and '-reg' in instr_type:
                return latency
        for instr_type, latency in instruction_latencies.items():
            if base_instruction in instr_type and '-imm' in instr_type:
                return latency
    
    return 0.0

def calculate_generic_instruction_latency(instruction, instruction_latencies):
    """Calculate latency for generic instructions"""
    base_instruction = instruction.split()[0] if ' ' in instruction else instruction
    for instr_type, latency in instruction_latencies.items():
        if base_instruction in instr_type:
            return latency
    return 0.0

# --- Function call latency calculation ---
def calculate_function_call_latency(line, function_latencies):
    """Calculate latency for function calls"""
    func_name = line.split("Call to ")[-1].split("(")[0].strip()
    return function_latencies.get(func_name, 0.0)

# --- Build call latency table from instruction latencies ---
def build_call_latency_table(instruction_latencies):
    """Extract call latency samples keyed by argument count (0/1/2/4) from latency CSV mapping.
    Returns a sorted list of (arg_count, latency)."""
    samples = {}
    for key, latency in instruction_latencies.items():
        key_lower = key.lower()
        if 'call' not in key_lower:
            continue
        # Heuristics: prefer explicit markers in key
        if 'void' in key_lower:
            samples[0] = float(latency)
            continue
        # Match formats like '1arg', '2args', '4args' possibly with separators
        import re
        m = re.search(r'(?:^|[-_])(?P<num>\d+)\s*args?\b', key_lower)
        if not m:
            # Fallback: trailing number token such as -1 or _2 at end
            m = re.search(r'(?:-|_)(\d+)(?!.*\d)', key_lower)
        if m:
            try:
                n = int(m.group('num') if 'num' in m.groupdict() else m.group(1))
                samples[n] = float(latency)
            except Exception:
                pass
    return sorted(samples.items())

def interpolate_call_latency(arg_count, call_latency_table, instruction_latencies):
    """Linearly interpolate (or extrapolate) call latency based on known samples.
    call_latency_table is a sorted list of (args, latency)."""
    # If no table provided, try to build from instruction_latencies
    if not call_latency_table:
        call_latency_table = build_call_latency_table(instruction_latencies)
    if not call_latency_table:
        return 0.0
    # Exact match
    for n, lat in call_latency_table:
        if arg_count == n:
            return lat
    # Below smallest -> extrapolate using first two if possible
    if arg_count < call_latency_table[0][0]:
        if len(call_latency_table) >= 2:
            (x1, y1), (x2, y2) = call_latency_table[0], call_latency_table[1]
            if x2 != x1:
                slope = (y2 - y1) / (x2 - x1)
                return y1 + slope * (arg_count - x1)
        return call_latency_table[0][1]
    # Above largest -> extrapolate using last two if possible
    if arg_count > call_latency_table[-1][0]:
        if len(call_latency_table) >= 2:
            (x1, y1), (x2, y2) = call_latency_table[-2], call_latency_table[-1]
            if x2 != x1:
                slope = (y2 - y1) / (x2 - x1)
                return y2 + slope * (arg_count - x2)
        return call_latency_table[-1][1]
    # Between two samples -> interpolate
    for i in range(1, len(call_latency_table)):
        x1, y1 = call_latency_table[i - 1]
        x2, y2 = call_latency_table[i]
        if x1 <= arg_count <= x2 and x2 != x1:
            slope = (y2 - y1) / (x2 - x1)
            return y1 + slope * (arg_count - x1)
    # Fallback
    return call_latency_table[-1][1]

# --- Main metrics calculation ---
def calculate_metrics(cache_stats_file, demarcated_file, instruction_latencies, function_latencies, memory_latencies):
    """Calculate all metrics from demarcated trace file"""
    # Parse cache statistics
    load_hits, load_misses, store_hits, store_misses = parse_cache_stats(cache_stats_file)
    total_hits = load_hits + store_hits
    total_misses = load_misses + store_misses
    mem_accesses = total_hits + total_misses
    total_mem_latency = calculate_memory_latency(load_hits, load_misses, store_hits, store_misses, memory_latencies)
    
    # Process trace file
    instr_count = 0
    total_cycles = 0.0
    
    # Build call latency table once
    call_latency_table = build_call_latency_table(instruction_latencies)

    with open(demarcated_file, 'r') as f:
        for line in f:
            l = line.strip()
            if not l or l == "Function | Instruction | Operands" or l == "EOF":
                continue
            
            if l.startswith("LOAD ") or l.startswith("STORE "):
                continue
            
            if l.startswith("Call to ") or l.startswith("CALL "):
                total_cycles += calculate_function_call_latency(l, function_latencies)
                continue
            
            # Count instruction and calculate its latency
            instr_count += 1
            
            # Parse instruction type from the line
            parts = l.split("|")
            if len(parts) >= 2:
                instruction = parts[1].strip()
                operands = parts[2].strip() if len(parts) > 2 else ""
                total_cycles += calculate_instruction_latency(instruction, operands, instruction_latencies, call_latency_table)
    
    # Add memory latency based on cache performance
    total_cycles += total_mem_latency
    
    print("[Metrics calculation]")
    print(f"Load Hits: {load_hits}, Load Misses: {load_misses}")
    print(f"Store Hits: {store_hits}, Store Misses: {store_misses}")
    print(f"Total Hits: {total_hits}, Total Misses: {total_misses}")
    print(f"llvm instruction count,{instr_count}")
    print(f"llvm memory instructions,{mem_accesses}")
    if total_cycles > 0:
        print(f"total cycles,{total_cycles}")
        print(f"memory latency: {total_mem_latency}")
    
    return total_hits, total_misses, mem_accesses, instr_count, total_cycles


def main():
    # Derive the corresponding *.ll.demarcated file
    if input_trace.endswith('.ll.tracelog.demarcated'):
        demarcated_file = input_trace.replace('.ll.tracelog.demarcated', '.ll.demarcated')
    else:
        raise ValueError("Input trace file must end with .ll.tracelog.demarcated")
    
    # Parse latency data if directory is provided
    instruction_latencies = {}
    function_latencies = {}
    memory_latencies = {}
    if latency_dir and os.path.exists(latency_dir):
        instruction_latencies = parse_ir_perf_csv(latency_dir)
        function_latencies = parse_api_perf_json(latency_dir)
        memory_latencies = parse_memory_latency_csv(latency_dir)
    
    # Calculate all metrics in a single pass
    total_hits, total_misses, mem_accesses, instr_count, total_cycles = calculate_metrics(
        cache_stats_file, input_trace, instruction_latencies, function_latencies, memory_latencies)
    
    # Parse cache stats again to get individual load/store counts for output
    load_hits, load_misses, store_hits, store_misses = parse_cache_stats(cache_stats_file)
    
    # Write to output files
    with open(cache_stats_file, 'w') as f:
        f.write(f"Load_Hits: {load_hits}\n")
        f.write(f"Load_Misses: {load_misses}\n")
        f.write(f"Store_Hits: {store_hits}\n")
        f.write(f"Store_Misses: {store_misses}\n")
        f.write(f"Hits: {total_hits}\n")  # Keep for backward compatibility
        f.write(f"Misses: {total_misses}\n")  # Keep for backward compatibility
    with open(llvm_metrics_file, 'w') as f:
        f.write(f"llvm instruction count,{instr_count}\n")
        f.write(f"llvm memory instructions,{mem_accesses}\n")
        if total_cycles > 0:
            f.write(f"total cycles,{total_cycles}\n")

if __name__ == "__main__":
    main()

