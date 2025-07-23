// trace-ir-instrs/TraceIRInstrs.cpp
#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include <vector>
#include <set>

using namespace llvm;

// Configurable start/end function names
static cl::opt<std::string>
    StartFunction("trace-ir-start", cl::desc("Name of function to start tracing"), cl::init("nf_core_process"));
static cl::opt<std::string>
    EndFunction("trace-ir-end", cl::desc("Name of function to end tracing"), cl::init("exit@plt"));

namespace {

struct TraceIRInstrs : public ModulePass {
    static char ID;
    TraceIRInstrs() : ModulePass(ID) {}

    // Runtime hook declarations
    Type *VoidTy, *Int1Ty, *I8PtrTy, *I32Ty;
    Function *setIsTracing, *checkTracingClosed, *traceClose, *traceInst, *traceCall, *traceMem, *openTraceFile;
    std::set<std::string> runtimeFnNames;

    void declareRuntimeHooks(Module &M) {
        LLVMContext &Ctx = M.getContext();
        VoidTy = Type::getVoidTy(Ctx);
        Int1Ty = Type::getInt1Ty(Ctx);
        I8PtrTy = Type::getInt8PtrTy(Ctx);
        I32Ty = Type::getInt32Ty(Ctx);
        FunctionType *SetTracingTy = FunctionType::get(VoidTy, {Int1Ty}, false);
        FunctionType *CheckClosedTy = FunctionType::get(VoidTy, {}, false);
        FunctionType *TraceCloseTy = FunctionType::get(VoidTy, {}, false);
        FunctionType *TraceInstTy = FunctionType::get(VoidTy, {I8PtrTy, I8PtrTy}, false);
        FunctionType *TraceCallTy = FunctionType::get(VoidTy, {I8PtrTy, I8PtrTy, I8PtrTy}, true);
        FunctionType *TraceMemTy = FunctionType::get(VoidTy, {I8PtrTy, I8PtrTy, I8PtrTy}, false);
        FunctionType *OpenTraceFileTy = FunctionType::get(VoidTy, {}, false);
        setIsTracing = cast<Function>(M.getOrInsertFunction("set_is_tracing", SetTracingTy).getCallee());
        checkTracingClosed = cast<Function>(M.getOrInsertFunction("check_tracing_closed", CheckClosedTy).getCallee());
        traceClose = cast<Function>(M.getOrInsertFunction("trace_close_log", TraceCloseTy).getCallee());
        traceInst = cast<Function>(M.getOrInsertFunction("trace_inst_log", TraceInstTy).getCallee());
        traceCall = cast<Function>(M.getOrInsertFunction("trace_call_log", TraceCallTy).getCallee());
        traceMem = cast<Function>(M.getOrInsertFunction("trace_mem_log", TraceMemTy).getCallee());
        openTraceFile = cast<Function>(M.getOrInsertFunction("open_trace_file", OpenTraceFileTy).getCallee());
        // Set of runtime function names to skip
        runtimeFnNames = {
            "set_is_tracing",
            "check_tracing_closed",
            "trace_close_log",
            "trace_inst_log",
            "trace_call_log",
            "trace_mem_log"
        };
    }

    // Helper: should we skip instrumentation for this call?
    bool isRuntimeCall(CallInst *call) {
        if (Function *callee = call->getCalledFunction()) {
            return runtimeFnNames.count(callee->getName().str()) > 0;
        }
        return false;
    }

    // Instrument a single basic block
    bool instrumentBasicBlock(Function &F, BasicBlock &BB) {
        bool modified = false;
        // Collect PHI nodes
        std::vector<PHINode*> phiNodes;
        auto it = BB.begin();
        while (auto *phi = dyn_cast<PHINode>(it)) {
            phiNodes.push_back(phi);
            ++it;
        }
        // Instrument PHI nodes after the last PHI node
        if (!phiNodes.empty()) {
            Instruction *insertAfter = phiNodes.back();
            IRBuilder<> builder(insertAfter->getNextNode());
            for (PHINode *phi : phiNodes) {
                Value *fnStr = builder.CreateGlobalStringPtr(F.getName());
                Value *opStr = builder.CreateGlobalStringPtr(phi->getOpcodeName());
                builder.CreateCall(traceInst, {fnStr, opStr});
                modified = true;
            }
        }
        // Instrument the rest of the instructions (non-PHI)
        for (; it != BB.end(); ++it) {
            Instruction &I = *it;
            IRBuilder<> builder(&I);
            Value *fnStr = builder.CreateGlobalStringPtr(F.getName());
            Value *opStr = builder.CreateGlobalStringPtr(I.getOpcodeName());
            if (auto *call = dyn_cast<CallInst>(&I)) {
                if (isRuntimeCall(call)) continue; // Skip calls to runtime functions
                std::string fmt;
                std::vector<Value*> callArgs = {fnStr};
                callArgs.push_back(builder.CreateGlobalStringPtr(call->getCalledFunction() ? call->getCalledFunction()->getName() : "<indirect>"));
                std::vector<Value*> valueArgs;
                for (unsigned i = 0; i < call->getNumArgOperands(); ++i) {
                    Value *arg = call->getArgOperand(i);
                    if (arg->getType()->isIntegerTy()) {
                        fmt += "%ld, ";
                        valueArgs.push_back(builder.CreateSExtOrBitCast(arg, builder.getInt64Ty()));
                    } else if (arg->getType()->isFloatingPointTy()) {
                        fmt += "%f, ";
                        valueArgs.push_back(builder.CreateFPExt(arg, builder.getDoubleTy()));
                    } else if (auto *cda = dyn_cast<ConstantDataArray>(arg)) {
                        if (cda->isString()) {
                            fmt += "%s, ";
                            valueArgs.push_back(builder.CreateGlobalStringPtr(cda->getAsString()));
                        } else {
                            fmt += "%p, ";
                            valueArgs.push_back(builder.CreatePointerCast(arg, I8PtrTy));
                        }
                    } else if (arg->getType()->isPointerTy()) {
                        fmt += "%p, ";
                        valueArgs.push_back(builder.CreatePointerCast(arg, I8PtrTy));
                    } else {
                        fmt += "<unk>, ";
                    }
                }
                Value *fmtStr = builder.CreateGlobalStringPtr(fmt.substr(0, fmt.size() - 2));
                callArgs.push_back(fmtStr);
                callArgs.insert(callArgs.end(), valueArgs.begin(), valueArgs.end());
                builder.CreateCall(traceCall, callArgs);
                modified = true;
            } else if (auto *load = dyn_cast<LoadInst>(&I)) {
                Value *addr = builder.CreatePointerCast(load->getPointerOperand(), I8PtrTy);
                Value *typeStr = builder.CreateGlobalStringPtr("LOAD");
                builder.CreateCall(traceMem, {fnStr, typeStr, addr});
                modified = true;
            } else if (auto *store = dyn_cast<StoreInst>(&I)) {
                Value *addr = builder.CreatePointerCast(store->getPointerOperand(), I8PtrTy);
                Value *typeStr = builder.CreateGlobalStringPtr("STORE");
                builder.CreateCall(traceMem, {fnStr, typeStr, addr});
                modified = true;
            }
            builder.CreateCall(traceInst, {fnStr, opStr});
            modified = true;
        }
        return modified;
    }

    // Instrument a single function
    bool instrumentFunction(Function &F) {
        bool modified = false;
        StringRef fname = F.getName();
        // Insert set_is_tracing(true) at entry of start function
        if (fname == StartFunction) {
            // Insert after all PHI nodes
            Instruction *insertPt = &*F.getEntryBlock().getFirstInsertionPt();
            IRBuilder<> builder(insertPt);
            builder.CreateCall(setIsTracing, {ConstantInt::getTrue(Int1Ty)});
            modified = true;
        }
        // Insert set_is_tracing(false) before every return in end function
        if (fname == EndFunction) {
            for (BasicBlock &BB : F) {
                for (auto it = BB.begin(), end = BB.end(); it != end; ++it) {
                    if (isa<ReturnInst>(&*it)) {
                        IRBuilder<> builder(&*it);
                        builder.CreateCall(setIsTracing, {ConstantInt::getFalse(Int1Ty)});
                        modified = true;
                    }
                }
            }
        }
        // Insert check_tracing_closed and trace_close_log before every return in main
        if (fname == "main") {
            for (BasicBlock &BB : F) {
                for (auto it = BB.begin(), end = BB.end(); it != end; ++it) {
                    if (isa<ReturnInst>(&*it)) {
                        IRBuilder<> builder(&*it);
                        builder.CreateCall(checkTracingClosed);
                        builder.CreateCall(traceClose);
                        modified = true;
                    }
                }
            }
            // Insert open_trace_file at the beginning of main
            Instruction *insertPt = &*F.getEntryBlock().getFirstInsertionPt();
            IRBuilder<> builder(insertPt);
            builder.CreateCall(openTraceFile);
        }
        // Instrument all basic blocks
        for (BasicBlock &BB : F) {
            if (instrumentBasicBlock(F, BB))
                modified = true;
        }
        return modified;
    }

    bool runOnModule(Module &M) override {
        declareRuntimeHooks(M);
        bool modified = false;
        for (Function &F : M) {
            if (F.isDeclaration()) continue;
            if (instrumentFunction(F))
                modified = true;
        }
        return modified;
    }
};

char TraceIRInstrs::ID = 0;
static RegisterPass<TraceIRInstrs> X("trace-ir-instrs", "Trace IR Instructions Pass", false, false);

} // namespace 