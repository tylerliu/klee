# trace-ir-instrs

This directory contains an LLVM instrumentation pass and a simple runtime for tracing LLVM IR instructions at runtime.

## What is this?
- **TraceIRInstrs.cpp**: An LLVM pass that instruments every instruction in a function to call a tracing function (`trace_inst`) with the instruction's opcode name.
- **TraceRuntime.cpp**: A simple runtime implementation of `trace_inst` that prints the opcode name to standard output.
- **CMakeLists.txt**: Build configuration for the pass and runtime.

## Building

1. Make sure you have LLVM (version 10 or later recommended) and CMake installed.
2. Build the pass and runtime:
   ```sh
   cd klee/trace-ir-instrs
   mkdir build && cd build
   cmake ..
   make
   ```
   This will produce:
   - `libTraceIRInstrs.so` (the LLVM pass plugin)
   - `libTraceRuntime.a` (the runtime library)

## Usage

### 1. Instrument your LLVM IR

To instrument your LLVM IR with this pass, use the following command:

```sh
opt -load ./libTraceIRInstrs.so -trace-ir-instrs -S input.ll -o output.ll \
    -trace-ir-start=<start_function> -trace-ir-end=<end_function>
```

- `-trace-ir-start` specifies the function where tracing should start (default: `nf_core_process`).
- `-trace-ir-end` specifies the function where tracing should end (default: `exit@plt`).
- If you omit these options, the defaults will be used.

**Example with custom start/end:**
```sh
opt -load ./libTraceIRInstrs.so -trace-ir-instrs -S input.ll -o output.ll \
    -trace-ir-start=my_start_fn -trace-ir-end=my_end_fn
```

This will instrument your IR so that tracing is enabled when entering `my_start_fn` and disabled when exiting `my_end_fn`. The trace will be written to `trace.out` at runtime.

### 2. Link with the runtime and build executable

```sh
clang++ output.ll ../libTraceRuntime.a -o traced_program
```

This ensures all trace output is properly written to `trace.out`.

### 3. Run your instrumented program

```sh
./traced_program
```

You will see output like:
```
TRACE: add
TRACE: store
TRACE: load
... (one per instruction executed)
```

## References
- [AtomicCounter/AtomicCountPass/AtomicCount.cpp](https://github.com/pranith/AtomicCounter/blob/master/AtomicCountPass/AtomicCount.cpp)
- [cse231/part1/CountDynamicInstructions.cpp](https://github.com/WangYueFt/cse231/blob/master/part1/CountDynamicInstructions.cpp)

## Notes
- This pass uses the new LLVM pass manager (for `opt-new`).
- The runtime is minimal; you can extend `trace_inst` to collect statistics, write to a file, etc. 

## Notes on Standard Library or other Linked Library Calls

When your instrumented code calls a standard library function (e.g., `printf`, `malloc`, etc.), logging behaves as follows:

- **If the standard library is linked as native code (the usual case):**
  - The call instruction to the standard library function will be logged (as a `CALL` entry) if `is_tracing` is true.
  - The internal instructions of the standard library function will **not** be logged, because they are not instrumented by the LLVM pass.

- **If the standard library is linked as LLVM bitcode and the pass is applied to it:**
  - Both the call to the standard library function and the instructions inside the standard library implementation will be logged (if `is_tracing` is true).
  - This requires linking the standard library as bitcode (e.g., using `klee-uclibc`) and running the pass over those modules as well.

**Summary:**
- By default, only the call to a standard library function is logged, not its internal operations.
- To log inside standard library functions, you must link them as LLVM bitcode and instrument them with the pass. 
