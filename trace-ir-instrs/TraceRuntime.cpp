#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

extern "C" {
static FILE* trace_file = nullptr;
static bool is_tracing = false;

// Call this at the beginning of main
void open_trace_file() {
    if (!trace_file) {
        trace_file = fopen("trace.out", "w");
        if (!trace_file) {
            fprintf(stderr, "ERROR: Failed to open trace file\n");
            exit(1);
        } else {
            // Write header
            fprintf(trace_file, "Function | Instruction\n");
        }
    }
}

void set_is_tracing(bool val) {
    if (is_tracing == val) {
        fprintf(stderr, "ERROR: set_is_tracing called with value already set (%s)!\n", val ? "true" : "false");
        exit(1);
    }
    is_tracing = val;
}

void check_tracing_closed() {
    if (is_tracing) {
        fprintf(stderr, "ERROR: Tracing was not properly closed before program exit!\n");
        exit(1);
    }
}

void trace_inst_log(const char* fn, const char* op) {
    if (!is_tracing || !trace_file) return;
    fprintf(trace_file, "%s | %s\n", fn, op);
}
void trace_call_log(const char* fn, const char* callee, int argc, const char** argv) {
    if (!is_tracing || !trace_file) return;
    fprintf(trace_file, "CALL %s (", callee);
    for (int i = 0; i < argc; ++i) {
        fprintf(trace_file, "%s%s", argv[i], (i+1<argc)?", ":"");
    }
    fprintf(trace_file, ")\n");
}
void trace_mem_log(const char* fn, const char* type, const void* addr) {
    if (!is_tracing || !trace_file) return;
    fprintf(trace_file, "%s %p\n", type, addr);
}
void trace_close_log() {
    if (trace_file && trace_file != stderr) {
        fprintf(trace_file, "EOF\n");
        fclose(trace_file);
        trace_file = nullptr;
    }
}
} 