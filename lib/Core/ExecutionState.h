//===-- ExecutionState.h ----------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_EXECUTIONSTATE_H
#define KLEE_EXECUTIONSTATE_H

#include "klee/Expr/Constraints.h"
#include "klee/Expr/Expr.h"
#include "klee/ADT/ImmutableSet.h"
#include "klee/ADT/TreeStream.h"
#include "klee/Solver/Solver.h"
#include "klee/System/Time.h"
#include "klee/ADT/GetExprSymbols.h"

#include "AddressSpace.h"
#include "MergeHandler.h"
#include "../Module/LoopAnalysis.h"
#include "klee/Module/KInstIterator.h"

// TODO: generalize for otehr LLVM versions like the above
#include "llvm/Analysis/LoopInfo.h"

#include <map>
#include <memory>
#include <regex>
#include <set>
#include <vector>

namespace llvm {
class Function;
class BasicBlock;
} // namespace llvm

namespace klee {
class Array;
class CallPathNode;
struct Cell;
struct KFunction;
struct KInstruction;
class MemoryObject;
class PTreeNode;
struct InstructionInfo;
class TimingSolver;

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const MemoryMap &mm);

struct StackFrame {
  KInstIterator caller;
  KFunction *kf;
  CallPathNode *callPathNode;

  std::vector<const MemoryObject *> allocas;
  Cell *locals;

  /// Minimum distance to an uncovered instruction once the function
  /// returns. This is not a good place for this but is used to
  /// quickly compute the context sensitive minimum distance to an
  /// uncovered instruction. This value is updated by the StatsTracker
  /// periodically.
  unsigned minDistToUncoveredOnReturn;

  // For vararg functions: arguments not passed via parameter are
  // stored (packed tightly) in a local (alloca) memory object. This
  // is set up to match the way the front-end generates vaarg code (it
  // does not pass vaarg through as expected). VACopy is lowered inside
  // of intrinsic lowering.
  MemoryObject *varargs;

  StackFrame(KInstIterator caller, KFunction *kf);
  StackFrame(const StackFrame &s);
  ~StackFrame();
};

struct FunctionAlias {
  bool isRegex;
  std::regex nameRegex;
  std::string name;
  std::string alias;
};

struct FieldDescr {
  Expr::Width width;
  std::string type;
  std::string name;
  size_t addr;
  bool doTraceValueIn;
  bool doTraceValueOut;
  ref<Expr> inVal;
  ref<Expr> outVal;
  std::map<int, FieldDescr> fields;

  bool sameInvocationValue(const FieldDescr &other) const;
  bool eq(const FieldDescr &other) const;
};

struct CallArg {
  ref<Expr> expr;
  bool isPtr;
  llvm::Function *funPtr;
  std::string name;

  FieldDescr pointee;

  bool eq(const CallArg &other) const;
  bool sameInvocationValue(const CallArg &other) const;
};

struct RetVal {
  ref<Expr> expr;
  bool isPtr;
  llvm::Function *funPtr;

  FieldDescr pointee;

  bool eq(const RetVal &other) const;
};

struct CallExtraVal {
  ref<Expr> expr;
  std::string name;
  std::string prefix;
};

struct CallExtraPtr {
  size_t ptr;
  FieldDescr pointee;
  bool accessibleIn;
  bool accessibleOut;

  std::string name;
  std::string prefix;

  bool sameInvocationValue(const CallExtraPtr &other) const;
  bool eq(const CallExtraPtr &other) const;
};

struct CallExtraFPtr {
  size_t ptr;
  ref<Expr> inVal, outVal;
  Expr::Width width;
  llvm::Function *funPtr;
  std::string name;
  std::string prefix;
};

// TODO: Store assumptions increment as well. it is an important part of the
// call
// these assumptions allow then to correctly match and distinguish call path
// prefixes.
struct CallInfo {
  llvm::Function *f;
  std::vector<CallArg> args;
  std::map<size_t, CallExtraPtr> extraPtrs;
  std::vector<CallExtraVal> extraVals;
  std::vector<CallExtraFPtr> extraFPtrs;

  RetVal ret;
  bool returned;
  std::vector<ref<Expr>> callContext;
  std::vector<ref<Expr>> returnContext;
  llvm::DebugLoc callPlace;

  CallArg *getCallArgPtrp(ref<Expr> ptr);
  bool eq(const CallInfo &other) const;
  bool sameInvocation(const CallInfo *other) const;
  SymbolSet computeRetSymbolSet() const;
};

struct HavocInfo {
  std::string name;
  bool havoced;
  BitArray mask;
  const Array *value;
};

class ExecutionState;

/// @brief LoopInProcess keeps all the necessary information for
/// dynamic loop invariant deduction.
class LoopInProcess {
public:
  /// @brief Required by klee::ref-managed objects
  /// This count also determines how many paths are in the loop.
  class ReferenceCounter _refCount;

private:
  ref<LoopInProcess> outer;
  const llvm::Loop *loop; // Owner: KFunction::loopInfo
  // No circular dependency here: the restartState must not have
  // loop in process.
  std::unique_ptr<ExecutionState> restartState; // Owner.
  bool lastRoundUpdated;
  // Owner for the bitarrays.
  StateByteMask changedBytes;
  // std::set<const MemoryObject *> changedObjects;

  ExecutionState *makeRestartState();

public:
  // Captures ownership of the _headerState.
  // TODO: rewrite in terms of std::uniquePtr
  LoopInProcess(const llvm::Loop *_loop, std::unique_ptr<ExecutionState> &&_headerState,
                const ref<LoopInProcess> &_outer);
  ~LoopInProcess();

  void updateChangedObjects(const ExecutionState &current,
                            TimingSolver *solver);
  ExecutionState *nextRoundState(bool *analysisFinished);

  const llvm::Loop *getLoop() const { return loop; }
  const StateByteMask &getChangedBytes() const { return changedBytes; }
  const ExecutionState &getEntryState() const { return *restartState; }
  const ref<LoopInProcess> &getOuter() const { return outer; }
};

/// Contains information related to unwinding (Itanium ABI/2-Phase unwinding)
class UnwindingInformation {
public:
  enum class Kind {
    SearchPhase, // first phase
    CleanupPhase // second phase
  };

private:
  const Kind kind;

public:
  // _Unwind_Exception* of the thrown exception, used in both phases
  ref<ConstantExpr> exceptionObject;

  Kind getKind() const { return kind; }

  explicit UnwindingInformation(ref<ConstantExpr> exceptionObject, Kind k)
      : kind(k), exceptionObject(exceptionObject) {}
  virtual ~UnwindingInformation() = default;

  virtual std::unique_ptr<UnwindingInformation> clone() const = 0;
};

struct SearchPhaseUnwindingInformation : public UnwindingInformation {
  // Keeps track of the stack index we have so far unwound to.
  std::size_t unwindingProgress;

  // MemoryObject that contains a serialized version of the last executed
  // landingpad, so we can clean it up after the personality fn returns.
  MemoryObject *serializedLandingpad = nullptr;

  SearchPhaseUnwindingInformation(ref<ConstantExpr> exceptionObject,
                                  std::size_t const unwindingProgress)
      : UnwindingInformation(exceptionObject,
                             UnwindingInformation::Kind::SearchPhase),
        unwindingProgress(unwindingProgress) {}

  std::unique_ptr<UnwindingInformation> clone() const {
    return std::make_unique<SearchPhaseUnwindingInformation>(*this);
  }

  static bool classof(const UnwindingInformation *u) {
    return u->getKind() == UnwindingInformation::Kind::SearchPhase;
  }
};

struct CleanupPhaseUnwindingInformation : public UnwindingInformation {
  // Phase 1 will try to find a catching landingpad.
  // Phase 2 will unwind up to this landingpad or return from
  // _Unwind_RaiseException if none was found.

  // The selector value of the catching landingpad that was found
  // during the search phase.
  ref<ConstantExpr> selectorValue;

  // Used to know when we have to stop unwinding and to
  // ensure that unwinding stops at the frame for which
  // we first found a handler in the search phase.
  const std::size_t catchingStackIndex;

  CleanupPhaseUnwindingInformation(ref<ConstantExpr> exceptionObject,
                                   ref<ConstantExpr> selectorValue,
                                   const std::size_t catchingStackIndex)
      : UnwindingInformation(exceptionObject,
                             UnwindingInformation::Kind::CleanupPhase),
        selectorValue(selectorValue),
        catchingStackIndex(catchingStackIndex) {}

  std::unique_ptr<UnwindingInformation> clone() const {
    return std::make_unique<CleanupPhaseUnwindingInformation>(*this);
  }

  static bool classof(const UnwindingInformation *u) {
    return u->getKind() == UnwindingInformation::Kind::CleanupPhase;
  }
};

/// @brief ExecutionState representing a path under exploration
class ExecutionState {
public:
  // copy ctor
  ExecutionState(const ExecutionState &state);

  using stack_ty = std::vector<StackFrame>;

private:

  // function alias and hardware intercepts related states
  std::vector<FunctionAlias> fnAliases;
  std::map<uint64_t, std::string> readsIntercepts;
  std::map<uint64_t, std::string> writesIntercepts;

public:
  // Execution - Control Flow specific

  /// @brief Pointer to instruction to be executed after the current
  /// instruction
  KInstIterator pc;

  /// @brief Pointer to instruction which is currently executed
  KInstIterator prevPC;

  /// @brief Stack representing the current instruction stream
  stack_ty stack;

  /// @brief Remember from which Basic Block control flow arrived
  /// (i.e. to select the right phi values)
  std::uint32_t incomingBBIndex;

  // Overall state of the state - Data specific

  /// @brief Exploration depth, i.e., number of times KLEE branched for this state
  std::uint32_t depth;

  /// @brief Address space used by this state (e.g. Global and Heap)
  AddressSpace addressSpace;

  /// @brief Information necessary for loop invariant induction.
  /// This is a stack capable of tracking loop nests.
  /// Owner.
  ref<LoopInProcess> loopInProcess;

  /// @brief A message from the final round of loop analysis to the
  /// klee_induce_invariants handler, to allow analysed loops
  /// skip it with no effect.
  ImmutableSet<const llvm::Loop *> analysedLoops;

  /// @brief This pointer keeps a copy of the state in case
  ///  we will need to process this loop. Owner.
  std::unique_ptr<ExecutionState> executionStateForLoopInProcess;

  /// @brief Constraints collected so far
  ConstraintSet constraints;

  /// @brief Flag to see if llvm instruction tracing is on
  int isTracing = 0;

  /// @brief List of Instructions executed so far
  std::vector<llvm::Instruction *> callPathInstr;

  /// @brief Call Stack with unmangled Function call names. This
  /// is recorded only for the portions of the call path when we are inside
  /// call-trace-instr-startfn (see main.cpp in tools/klee)
  std::vector<std::string> traceCallStack;

  /// @brief Hash Map between CallStack and and llvm Instruction
  // TODO: Change to std::map
  std::vector<std::pair<std::vector<std::string>, llvm::Instruction *>>
      stackInstrMap;

  /// Statistics and information

  /// @brief Metadata utilized and collected by solvers for this state
  mutable SolverQueryMetaData queryMetaData;

  /// @brief History of complete path: represents branches taken to
  /// reach/create this state (both concrete and symbolic)
  TreeOStream pathOS;

  /// @brief History of symbolic path: represents symbolic branches
  /// taken to reach/create this state
  TreeOStream symPathOS;

  /// @brief Set containing which lines in which files are covered by this state
  std::map<const std::string *, std::set<std::uint32_t>> coveredLines;

  /// @brief Pointer to the process tree of the current state
  /// Copies of ExecutionState should not copy ptreeNode
  PTreeNode *ptreeNode = nullptr;

  /// @brief Ordered list of symbolics: used to generate test cases.
  //
  // FIXME: Move to a shared list structure (not critical).
  std::vector<std::pair<ref<const MemoryObject>, const Array *>> symbolics;

  /// @brief The list of possibly havoced memory locations with their names
  ///  and values placed at the last havoc event.
  std::map<ref<const MemoryObject>, HavocInfo> havocs;

  /// @brief The list of registered havoc mem location names, used to guarantee
  ///  uniqueness of each name.
  std::set<std::string> havocNames;

  /// @brief Set of used array names for this state.  Used to avoid collisions.
  std::set<std::string> arrayNames;

  std::vector<CallInfo> callPath;
  SymbolSet relevantSymbols;

  /// @brief: a flag indicating that the state is genuine and not
  ///  a product of some ancillary analysis, like loop-invariant search.
  bool doTrace;

  /// @brief: Whether to forgive the undeclared memory location changing their
  ///  value during loop invariant analysis.
  bool condoneUndeclaredHavocs;

  /// @brief The objects handling the klee_open_merge calls this state ran through
  std::vector<ref<MergeHandler>> openMergeStack;

  /// @brief The numbers of times this state has run through Executor::stepInstruction
  std::uint64_t steppedInstructions;

  std::map<std::string, std::map<int, ref<Expr>>> reused_symbols;

  unsigned int bpf_calls;

  /// @brief Counts how many instructions were executed since the last new
  /// instruction was covered.
  std::uint32_t instsSinceCovNew;

  /// @brief Keep track of unwinding state while unwinding, otherwise empty
  std::unique_ptr<UnwindingInformation> unwindingInformation;

  /// @brief the global state counter
  static std::uint32_t nextID;

  /// @brief the state id
  std::uint32_t id {0};

  /// @brief Whether a new instruction was covered in this state
  bool coveredNew;

  /// @brief Disables forking for this state. Set by user code
  bool forkDisabled;

private:
  void loopRepetition(const llvm::Loop *dstLoop, TimingSolver *solver,
                      bool *terminate);
  void loopEnter(const llvm::Loop *dstLoop);
  void loopExit(const llvm::Loop *srcLoop, bool *terminate);

public:
  #ifdef KLEE_UNITTEST
  // provide this function only in the context of unittests
  ExecutionState(){}
  #endif
  // only to create the initial state
  explicit ExecutionState(KFunction *kf);

  // XXX total hack, just used to make a state so solver can
  // use on structure
  ExecutionState(const std::vector<ref<Expr>> &assumptions);
  
  // no copy assignment, use copy constructor
  ExecutionState &operator=(const ExecutionState &) = delete;
  // no move ctor
  ExecutionState(ExecutionState &&) noexcept = delete;
  // no move assignment
  ExecutionState& operator=(ExecutionState &&) noexcept = delete;
  // dtor
  ~ExecutionState();

  ExecutionState *branch();

  void addHavocInfo(const MemoryObject *mo, const std::string &name);

  void pushFrame(KInstIterator caller, KFunction *kf);
  void popFrame();

  void addSymbolic(const MemoryObject *mo, const Array *array);
  
  void addConstraint(ref<Expr> e);

  bool merge(const ExecutionState &b);
  void dumpStack(llvm::raw_ostream &out) const;

  std::uint32_t getID() const { return id; };
  void setID() { id = nextID++; };

  std::string getFnAlias(std::string fn);
  void addFnAlias(std::string old_fn, std::string new_fn);
  void addFnRegexAlias(std::string fn_regex, std::string new_fn);
  void removeFnAlias(std::string fn);

  std::string getInterceptReader(uint64_t addr);
  std::string getInterceptWriter(uint64_t addr);
  void addReadsIntercept(uint64_t addr, std::string reader);
  void addWritesIntercept(uint64_t addr, std::string writer);
  
  bool isAccessibleAddr(ref<Expr> addr) const;
  ref<Expr> readMemoryChunk(ref<Expr> addr, Expr::Width width,
                            bool circumventInaccessibility) const;
  void traceArgValue(ref<Expr> val, std::string name);
  void traceExtraValue(ref<Expr> val, std::string name, std::string prefix);
  void traceArgPtr(ref<Expr> arg, Expr::Width width, std::string name,
                   std::string type, bool tracePointeeIn, bool tracePointeeOut);
  void traceArgFunPtr(ref<Expr> arg, std::string name);
  void traceRet();
  void traceRetPtr(Expr::Width width, bool tracePointee);
  void traceArgPtrField(ref<Expr> arg, int offset, Expr::Width width,
                        std::string name, bool doTraceValueIn,
                        bool doTraceValueOut);
  void traceArgPtrNestedField(ref<Expr> arg, int base_offset, int offset,
                              Expr::Width width, std::string name,
                              bool trace_in, bool trace_out);
  void traceExtraPtr(size_t ptr, Expr::Width width, std::string name,
                     std::string type, std::string prefix, bool trace_in,
                     bool trace_out);
  void traceExtraPtrField(size_t ptr, int offset, Expr::Width width,
                          std::string name, bool trace_in, bool trace_out);
  void traceExtraPtrNestedField(size_t ptr, int base_offset, int offset,
                                Expr::Width width, std::string name,
                                bool trace_in, bool trace_out);
  void traceExtraPtrNestedNestedField(size_t ptr, int base_base_offset,
                                      int base_offset, int offset,
                                      Expr::Width width, std::string name,
                                      bool trace_in, bool trace_out);
  void traceExtraFPtr(ref<Expr> ptr, Expr::Width width, std::string name,
                      std::string type, std::string prefix, bool trace_in,
                      bool trace_out);
  void traceRetPtrField(int offset, Expr::Width width, std::string name,
                        bool doTraceValue);
  void traceRetPtrNestedField(int base_offset, int offset, Expr::Width width,
                              std::string name);

  void recordRetConstraints(CallInfo *info) const;

  void dumpConstraints() const;
  ExecutionState *finishLoopRound(KFunction *kf);
  void updateLoopAnalysisForBlockTransfer(llvm::BasicBlock *dst,
                                          llvm::BasicBlock *src,
                                          TimingSolver *solver,
                                          bool *terminate);
  std::vector<ref<Expr>> relevantConstraints(SymbolSet symbols) const;
  void terminateState(ExecutionState **replace);
  void induceInvariantsForThisLoop(KInstruction *target);
  void startInvariantSearch();
};

struct ExecutionStateIDCompare {
  bool operator()(const ExecutionState *a, const ExecutionState *b) const {
    return a->getID() < b->getID();
  }
};

} // namespace klee

#endif /* KLEE_EXECUTIONSTATE_H */
