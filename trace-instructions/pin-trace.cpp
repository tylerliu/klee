/*BEGIN_LEGAL
Intel Open Source License

Copyright (c) 2002-2017 Intel Corporation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the Intel Corporation nor the names of its contributors may be used to
endorse or promote products derived from this software without
specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */
#include "pin.H"
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

std::ofstream trace;

/* ===================================================================== */
// Command line switches
/* ===================================================================== */
KNOB<std::string> KnobStartFn(KNOB_MODE_WRITEONCE, "pintool", "start-fn",
                         "nf_core_process",
                         "specify function at which to start tracing");
KNOB<std::string> KnobEndFn(KNOB_MODE_WRITEONCE, "pintool", "end-fn", "exit@plt",
                       "specify function at which to end tracing");

typedef struct {
  unsigned long ip;
  std::string function;
  std::string assembly;
  std::string category;
  std::map<LEVEL_BASE::REG, int> written_regs;
} instruction_data_t;

std::vector<std::pair<bool, unsigned long>> addresses;
std::vector<std::string> calls;
std::map<LEVEL_BASE::REG, std::string> regs;

bool call = false;

bool is_logging = false;

std::string start_fn = "";
std::string end_fn = "";

#define MAX_NDEVS 128
UINT8 *mapped_memory_addr[MAX_NDEVS] = {NULL};
UINT8 num_devices = 0;
void (*stub_hardware_write)(uint64_t addr, unsigned offset, unsigned size,
                            uint64_t value) = NULL;
uint64_t (*stub_hardware_read)(uint64_t addr, unsigned offset,
                               unsigned size) = NULL;
char *(*get_mapped_memory_ptr)(int) = NULL;
UINT8 (*get_num_devs)() = NULL;

VOID log_read_op(VOID *ip, UINT8 *addr, UINT32 size, THREADID tid,
                 CONTEXT *ctxt) {
#if 0
printf("intercepting read %p [%u]\n", addr, size);
fflush(stdout);
#endif
  if (0 != stub_hardware_read) {
    for (int i = 0; i < num_devices; ++i) {
      if (mapped_memory_addr[i] <= addr &&
          addr < mapped_memory_addr[i] + (1 << 20)) {
        // printf("interesting read\n");
        // fflush(stdout);
        // printf("read addr :%p (%p + %lu)\n", addr, mapped_memory_addr, addr -
        // mapped_memory_addr); printf("calling %p\n", stub_hardware_read);

        CALL_APPLICATION_FUNCTION_PARAM param;
        memset(&param, 0, sizeof(param));
        param.native = true;

        UINT64 value;

        PIN_CallApplicationFunction(
            ctxt, tid, CALLINGSTD_DEFAULT, AFUNPTR(stub_hardware_read), &param,
            PIN_PARG(uint64_t), &value, PIN_PARG(uint64_t),
            mapped_memory_addr[i], PIN_PARG(unsigned),
            addr - mapped_memory_addr[i], PIN_PARG(unsigned), size,
            PIN_PARG_END());

        memcpy(addr, &value, size);
      }
    }
  }
  if (is_logging)
    addresses.push_back(std::make_pair(0, (unsigned long)addr));
}

VOID intercept_write_op(VOID *ip, UINT8 *addr, UINT32 size, THREADID tid,
                        CONTEXT *ctxt) {
#if 0
    printf("intercepting write %p [%u]\n", addr, size);
    fflush(stdout);
#endif
  if (1 == size)
    return;
  if (0 == stub_hardware_write)
    return;
  for (int i = 0; i < num_devices; ++i) {
    if (mapped_memory_addr[i] <= addr &&
        addr < mapped_memory_addr[i] + (1 << 20)) {
      // printf("interesting write %d : %p[%u]\n", i, addr, size);
      // fflush(stdout);

      UINT64 value;
      memcpy(&value, addr, size);
      // printf("write addr :%p (%p + %lu)", addr, mapped_memory_addr[i], addr -
      // mapped_memory_addr[i]);

      CALL_APPLICATION_FUNCTION_PARAM param;
      memset(&param, 0, sizeof(param));
      param.native = true;

      UINT64 dummy_ret;
      // printf("calling the stub_hardware_write\n");

      PIN_CallApplicationFunction(
          ctxt, tid, CALLINGSTD_DEFAULT, AFUNPTR(stub_hardware_write), &param,
          PIN_PARG(uint64_t), &dummy_ret, PIN_PARG(uint64_t),
          mapped_memory_addr[i], PIN_PARG(unsigned),
          addr - mapped_memory_addr[i], PIN_PARG(unsigned), size,
          PIN_PARG(uint64_t), value, PIN_PARG_END());
    }
  }
}

VOID log_write_op(VOID *ip, VOID *addr) {
  if (is_logging)
    addresses.push_back(std::make_pair(1, (unsigned long)addr));
}

// This function is called before every instruction is executed
// and prints the IP
VOID log_instruction(CONTEXT *ctx, instruction_data_t *id) {
  if (call) {
    calls.push_back(id->function);
    call = false;
  }
  if (!is_logging)
    return;

  /* Printing values of all registers before the instruction was executed */
  for (std::map<LEVEL_BASE::REG, std::string>::iterator i = regs.begin();
       i != regs.end(); ++i) {

    trace << i->second << " (" << i->first << ")"
          << " = " << PIN_GetContextReg(ctx, i->first) << std::endl;
  }

  /* Commenting for now, unecessary for analysis */
  /*trace << "Number of Written Registers = " << id->written_regs.size() <<
  std::endl; for(std::map<LEVEL_BASE::REG,int>::iterator i =
  id->written_regs.begin(); i!= id->written_regs.end(); ++i) {

          if(regs.count(i->first)) {
                  trace<<"Write to " << regs[i->first] << std::endl;
          }
          else if(!(LEVEL_BASE::REG_is_flags(i->first))) trace<< "Register not
  found " << i->first << std::endl;
  }
  */

  trace << std::hex << std::uppercase << id->ip << " |";
  for (auto c : calls) {
    trace << " " << c;
  }

  trace << " | " << id->function << " | " << id->assembly << " |";

  for (auto a : addresses) {
    trace << " " << (a.first ? "w" : "r") << a.second;
  }
  addresses.clear();

  trace << std::endl;
}

VOID log_call() { call = true; }

VOID log_return() {
  assert((!calls.empty()) && "Return with no Call.");
  calls.pop_back();
}

// Pin calls this function every time a new instruction is encountered
VOID Instruction(INS ins, VOID *v) {
  // Insert a call to printins before every instruction
  instruction_data_t *id = new instruction_data_t();
  id->ip = INS_Address(ins);
  id->function = RTN_FindNameByAddress(id->ip);
  id->assembly = INS_Disassemble(ins);

  /* We don't print this anymore */
  id->category = CATEGORY_StringShort(INS_Category(ins));

  /* Getting written registers */
  for (unsigned int i = 1; i <= INS_MaxNumWRegs(ins); ++i) {
    id->written_regs[LEVEL_BASE::REG_FullRegName(INS_RegW(ins, i))] = 1;
  }

  // Instruments memory accesses using a predicated call, i.e.
  // the instrumentation is called iff the instruction will actually be
  // executed.
  //
  // On the IA-32 and Intel(R) 64 architectures conditional moves and REP
  // prefixed instructions appear as predicated instructions in Pin.
  UINT32 memOperands = INS_MemoryOperandCount(ins);
  // Iterate over each memory operand of the instruction.
  for (UINT32 memOp = 0; memOp < memOperands; memOp++) {
    if (INS_MemoryOperandIsRead(ins, memOp)) {
      INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)log_read_op,
                               IARG_INST_PTR, IARG_MEMORYOP_EA, memOp,
                               IARG_MEMORYREAD_SIZE, IARG_THREAD_ID,
                               IARG_CONTEXT, IARG_END);
    }
    // Note that in some architectures a single memory operand can be
    // both read and written (for instance incl (%eax) on IA-32)
    // In that case we instrument it once for read and once for write.
    if (INS_MemoryOperandIsWritten(ins, memOp)) {
      INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)log_write_op,
                               IARG_INST_PTR, IARG_MEMORYOP_EA, memOp,
                               IARG_END);
      if (!INS_IsProcedureCall(ins)) {
        INS_InsertPredicatedCall(ins, IPOINT_AFTER, (AFUNPTR)intercept_write_op,
                                 IARG_INST_PTR, IARG_MEMORYOP_EA, memOp,
                                 IARG_MEMORYWRITE_SIZE, IARG_THREAD_ID,
                                 IARG_CONTEXT, IARG_END);
      }
    }
  }

#define ACTUALLY_TRACING 1
#if ACTUALLY_TRACING

  INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)log_instruction, IARG_CONTEXT,
                 IARG_PTR, id, IARG_END);

  if (INS_IsRet(ins)) {
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)log_return, IARG_END);
  } else if (INS_IsProcedureCall(ins)) {
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)log_call, IARG_END);
  }
#endif // ACTUALLY_TRACING
}

VOID trace_before(CHAR *name, ADDRINT size) {
  printf("Start logging.\n");
  fflush(stdout);
  is_logging = true;

  /*Instantiating registers map */
  regs[LEVEL_BASE::REG_FullRegName(LEVEL_BASE::REG_RAX)] = "rax";
  regs[LEVEL_BASE::REG_FullRegName(LEVEL_BASE::REG_RBX)] = "rbx";
  regs[LEVEL_BASE::REG_FullRegName(LEVEL_BASE::REG_RDI)] = "rdi";
  regs[LEVEL_BASE::REG_FullRegName(LEVEL_BASE::REG_RSI)] = "rsi";
  regs[LEVEL_BASE::REG_FullRegName(LEVEL_BASE::REG_RDX)] = "rdx";
  regs[LEVEL_BASE::REG_FullRegName(LEVEL_BASE::REG_RCX)] = "rcx";
  regs[LEVEL_BASE::REG_FullRegName(LEVEL_BASE::REG_RBP)] = "rbp";
  regs[LEVEL_BASE::REG_FullRegName(LEVEL_BASE::REG_RSP)] = "rsp";
  regs[LEVEL_BASE::REG_FullRegName(LEVEL_BASE::REG_R8)] = "r8";
  regs[LEVEL_BASE::REG_FullRegName(LEVEL_BASE::REG_R9)] = "r9";
  regs[LEVEL_BASE::REG_FullRegName(LEVEL_BASE::REG_R10)] = "r10";
  regs[LEVEL_BASE::REG_FullRegName(LEVEL_BASE::REG_R11)] = "r11";
  regs[LEVEL_BASE::REG_FullRegName(LEVEL_BASE::REG_R12)] = "r12";
  regs[LEVEL_BASE::REG_FullRegName(LEVEL_BASE::REG_R13)] = "r13";
  regs[LEVEL_BASE::REG_FullRegName(LEVEL_BASE::REG_R14)] = "r14";
  regs[LEVEL_BASE::REG_FullRegName(LEVEL_BASE::REG_R15)] = "r15";
}

VOID trace_after(ADDRINT ret) { is_logging = false; }

VOID Image(IMG img, VOID *v) {

  RTN processRtn = RTN_FindByName(img, start_fn.c_str());
  if (RTN_Valid(processRtn)) {
    RTN_Open(processRtn);
    RTN_InsertCall(processRtn, IPOINT_BEFORE, (AFUNPTR)trace_before,
                   IARG_ADDRINT, start_fn.c_str(),
                   IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);
    RTN_Close(processRtn);
  } else {
  }
  processRtn = RTN_FindByName(img, end_fn.c_str());
  if (RTN_Valid(processRtn)) {
    RTN_Open(processRtn);
    RTN_InsertCall(processRtn, IPOINT_AFTER, (AFUNPTR)trace_after,
                   IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);
    RTN_Close(processRtn);
  }
}
// This function is called when the application exits
VOID Fini(INT32 code, VOID *v) {
  trace << "#eof" << std::endl;
  trace.close();
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage() {
  PIN_ERROR("This Pintool traces each instruction and memory access.\n" +
            KNOB_BASE::StringKnobSummary() + "\n");
  return -1;
}

static VOID imageLoad(IMG img, VOID *v) {
  // Just instrument the main image.
  if (!IMG_IsMainExecutable(img))
    return;

  for (SYM sym = IMG_RegsymHead(img); SYM_Valid(sym); sym = SYM_Next(sym)) {
    if (0 == strcmp(SYM_Name(sym).c_str(), "get_num_devs")) {
      *(ADDRINT *)&get_num_devs = SYM_Address(sym);
      // printf("pinned num devs: %p\n", get_num_devs);
    }
    if (0 == strcmp(SYM_Name(sym).c_str(), "get_mapped_memory_ptr")) {
      *(ADDRINT *)&get_mapped_memory_ptr = SYM_Address(sym);
      // printf("pinned mapped memory: %p\n", get_mapped_memory_ptr);
    }
    if (0 == strcmp(SYM_Name(sym).c_str(), "stub_hardware_read_wrapper")) {
      *(ADDRINT *)&stub_hardware_read = SYM_Address(sym);
      // printf("pinned read fun: %p\n", stub_hardware_read);
    }
    if (0 == strcmp(SYM_Name(sym).c_str(), "stub_hardware_write_wrapper")) {
      *(ADDRINT *)&stub_hardware_write = SYM_Address(sym);
      // printf("pinned write fun: %p\n", stub_hardware_write);
    }
  }

  if (0 != get_num_devs) {
    assert(get_mapped_memory_ptr);
    assert(stub_hardware_read);
    assert(stub_hardware_write);

    // printf("calling num_devs\n");
    // fflush(stdout);
    num_devices = (UINT8)get_num_devs();
    // printf("got %d\n", num_devices);
    // fflush(stdout);
    assert(num_devices <= MAX_NDEVS);
    for (int i = 0; i < num_devices; ++i) {
      // printf("calling get_mapped_memory for %d\n", i);
      // fflush(stdout);
      mapped_memory_addr[i] = (UINT8 *)get_mapped_memory_ptr(i);
      // printf("got: %p\n", mapped_memory_addr[i]);
      // fflush(stdout);
    }
    // printf("init done\n");
    // fflush(stdout);
  }
}

EXCEPT_HANDLING_RESULT ExceptionHandler(THREADID tid,
                                        EXCEPTION_INFO *pExceptInfo,
                                        PHYSICAL_CONTEXT *pPhysCtxt, VOID *v) {
  EXCEPTION_CODE c = PIN_GetExceptionCode(pExceptInfo);
  EXCEPTION_CLASS cl = PIN_GetExceptionClass(c);
  std::cout << "Exception class " << cl;
  std::cout << PIN_ExceptionToString(pExceptInfo);
  return EHR_UNHANDLED;
}

EXCEPT_HANDLING_RESULT GlobalHandler2(THREADID threadIndex,
                                      EXCEPTION_INFO *pExceptInfo,
                                      PHYSICAL_CONTEXT *pPhysCtxt, VOID *v) {
	std::cout << "GlobalHandler2: Caught exception. "
		<< PIN_ExceptionToString(pExceptInfo) << std::endl;
  return EHR_CONTINUE_SEARCH;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char *argv[]) {
  trace.open("trace.out", std::ofstream::out);
  trace << "IP | Call Stack | Function | Instruction | Memory Accesses"
        << std::endl;

  // Load debug symbols.
  PIN_InitSymbols();

  // Initialize pin
  if (PIN_Init(argc, argv))
    return Usage();

  // Knobs
  start_fn = KnobStartFn.Value();
  end_fn = KnobEndFn.Value();

  // Exception handler
  PIN_AddInternalExceptionHandler(GlobalHandler2, NULL);

  // Register Instruction to be called to instrument instructions
  INS_AddInstrumentFunction(Instruction, 0);

  // Register Image to be called to preprocess images
  // i.e. scan through the symbols
  IMG_AddInstrumentFunction(Image, 0);
  // Register imageLoad to be called to preprocess images,
  // i.e. scan through the symbols
  IMG_AddInstrumentFunction(imageLoad, 0);

  // Register here your exception handler function
  PIN_AddInternalExceptionHandler(ExceptionHandler, NULL);

  // Register Fini to be called when the application exits
  PIN_AddFiniFunction(Fini, 0);

  // Start the program, never returns
  PIN_StartProgram();

  return 0;
}
