//===-- ExecutionState.cpp ------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ExecutionState.h"

#include "Memory.h"
#include "../Module/LoopAnalysis.h"
#include "klee/Expr/Expr.h"
#include "TimingSolver.h"
#include "klee/Module/Cell.h"
#include "klee/Module/InstructionInfoTable.h"
#include "klee/Module/KInstruction.h"
#include "klee/Module/KModule.h"
#include "klee/Support/Casting.h"
#include "klee/Support/OptionCategories.h"
#include "klee/Support/ErrorHandling.h"

#include "klee/Expr/Expr.h"
#include "klee/Expr/ExprBuilder.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <cstdio>
#include <iomanip>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <cstdarg>
#include <tuple>

using namespace llvm;
using namespace klee;

namespace {
cl::opt<bool> DebugLogStateMerge("debug-log-state-merge");
}

namespace klee {
  extern cl::opt<bool> UseIncompleteMerge;
}

/***/

std::uint32_t ExecutionState::nextID = 1;

/***/

StackFrame::StackFrame(KInstIterator _caller, KFunction *_kf)
    : caller(_caller), kf(_kf), callPathNode(0), minDistToUncoveredOnReturn(0),
      varargs(0) {
  locals = new Cell[kf->numRegisters];
}

StackFrame::StackFrame(const StackFrame &s)
    : caller(s.caller), kf(s.kf), callPathNode(s.callPathNode),
      allocas(s.allocas),
      minDistToUncoveredOnReturn(s.minDistToUncoveredOnReturn),
      varargs(s.varargs) {
  locals = new Cell[s.kf->numRegisters];
  for (unsigned i = 0; i < s.kf->numRegisters; i++)
    locals[i] = s.locals[i];
}

StackFrame::~StackFrame() { delete[] locals; }

/***/

ExecutionState::ExecutionState(KFunction *kf)
    : pc(kf->instructions), 
      prevPC(pc),
      depth(0),
      executionStateForLoopInProcess(nullptr),
      ptreeNode(nullptr),
      relevantSymbols(), 
      doTrace(true), 
      condoneUndeclaredHavocs(false), 
      steppedInstructions(0),
      bpf_calls(0),
      instsSinceCovNew(0),
      coveredNew(false),
      forkDisabled(false) {
  pushFrame(nullptr, kf);
}

ExecutionState::ExecutionState(const std::vector<ref<Expr> > &assumptions)
    : executionStateForLoopInProcess(nullptr),
      constraints(assumptions), 
      ptreeNode(0), 
      relevantSymbols(), 
      doTrace(true),
      condoneUndeclaredHavocs(false), 
      bpf_calls(0) {}

ExecutionState::~ExecutionState() {
  for (const auto &cur_mergehandler: openMergeStack){
    cur_mergehandler->removeOpenState(this);
  }

  while (!stack.empty())
    popFrame();
}

ExecutionState::ExecutionState(const ExecutionState &state)
    : fnAliases(state.fnAliases), 
      readsIntercepts(state.readsIntercepts),
      writesIntercepts(state.writesIntercepts), 
      pc(state.pc),
      prevPC(state.prevPC), 
      stack(state.stack),
      incomingBBIndex(state.incomingBBIndex),

      depth(state.depth),
      addressSpace(state.addressSpace), 
      loopInProcess(state.loopInProcess),
      analysedLoops(state.analysedLoops), 
      executionStateForLoopInProcess(nullptr),
      constraints(state.constraints),
      isTracing(state.isTracing),
      callPathInstr(state.callPathInstr), 
      traceCallStack(state.traceCallStack), 
      stackInstrMap(state.stackInstrMap), 

      pathOS(state.pathOS), 
      symPathOS(state.symPathOS),

      coveredLines(state.coveredLines),
      ptreeNode(state.ptreeNode),
      symbolics(state.symbolics),
      havocs(state.havocs), 
      havocNames(state.havocNames),
      arrayNames(state.arrayNames),
      callPath(state.callPath),
      relevantSymbols(state.relevantSymbols), 
      doTrace(state.doTrace),
      condoneUndeclaredHavocs(state.condoneUndeclaredHavocs),
      openMergeStack(state.openMergeStack),
      steppedInstructions(state.steppedInstructions),
      bpf_calls(state.bpf_calls),
      instsSinceCovNew(state.instsSinceCovNew),
      unwindingInformation(state.unwindingInformation
                            ? state.unwindingInformation->clone()
                            : nullptr),
      coveredNew(state.coveredNew),
      forkDisabled(state.forkDisabled)
{
  for (const auto &cur_mergehandler: openMergeStack)
    cur_mergehandler->addOpenState(this);

  if (!loopInProcess.isNull()) {
    LOG_LA("Cloning ES " << (void *)this << " from " << (void *)&state);
  }
}

void ExecutionState::addHavocInfo(const MemoryObject *mo,
                                  const std::string &name) {
  havocs[mo].name = name;
  havocs[mo].havoced = false;
  havocs[mo].mask = BitArray();
}

ExecutionState *ExecutionState::branch() {
  depth++;

  auto *falseState = new ExecutionState(*this);
  falseState->setID();
  falseState->coveredNew = false;
  falseState->coveredLines.clear();

  return falseState;
}

void ExecutionState::pushFrame(KInstIterator caller, KFunction *kf) {
  stack.emplace_back(StackFrame(caller, kf));
}

void ExecutionState::popFrame() {
  const StackFrame &sf = stack.back();
  for (const auto * memoryObject : sf.allocas)
    addressSpace.unbindObject(memoryObject);
  stack.pop_back();
}

void ExecutionState::addSymbolic(const MemoryObject *mo, const Array *array) {
  symbolics.emplace_back(ref<const MemoryObject>(mo), array);
}
///

std::string ExecutionState::getFnAlias(std::string fn) {
  for (auto &candidate : fnAliases) {
    if (candidate.isRegex) {
      if (std::regex_match(fn, candidate.nameRegex)) {
        return candidate.alias;
      }
    } else if (fn == candidate.name) {
      return candidate.alias;
    }
  }

  return "";
}

void ExecutionState::addFnAlias(std::string old_fn, std::string new_fn) {
  removeFnAlias(old_fn);

  FunctionAlias alias{.isRegex = false,
                      .nameRegex = std::regex(""),
                      .name = old_fn,
                      .alias = new_fn};
  fnAliases.push_back(alias);
}

void ExecutionState::addFnRegexAlias(std::string fn_regex, std::string new_fn) {
  removeFnAlias(fn_regex);

  FunctionAlias alias = {.isRegex = true,
                         .nameRegex = std::regex(fn_regex),
                         .name = fn_regex,
                         .alias = new_fn};
  fnAliases.push_back(alias);
}

void ExecutionState::removeFnAlias(std::string fn) {
  fnAliases.erase(std::remove_if(fnAliases.begin(), fnAliases.end(),
                                 [fn](FunctionAlias candidate) {
                                   return candidate.name == fn;
                                 }),
                  fnAliases.end());
}

std::string ExecutionState::getInterceptReader(uint64_t addr) {
  auto it = readsIntercepts.find(addr);
  if (it == readsIntercepts.end()) {
    return "";
  }

  return it->second;
}

std::string ExecutionState::getInterceptWriter(uint64_t addr) {
  auto it = writesIntercepts.find(addr);
  if (it == writesIntercepts.end()) {
    return "";
  }

  return it->second;
}

void ExecutionState::addReadsIntercept(uint64_t addr, std::string reader) {
  readsIntercepts[addr] = reader;
}

void ExecutionState::addWritesIntercept(uint64_t addr, std::string writer) {
  writesIntercepts[addr] = writer;
}

/**/

llvm::raw_ostream &klee::operator<<(llvm::raw_ostream &os,
                                    const MemoryMap &mm) {
  os << "{";
  MemoryMap::iterator it = mm.begin();
  MemoryMap::iterator ie = mm.end();
  if (it != ie) {
    os << "MO" << it->first->id << ":" << it->second.get();
    for (++it; it != ie; ++it)
      os << ", MO" << it->first->id << ":" << it->second.get();
  }
  os << "}";
  return os;
}

bool ExecutionState::merge(const ExecutionState &b) {
  if (DebugLogStateMerge)
    llvm::errs() << "-- attempting merge of A:" << this << " with B:" << &b
                 << "--\n";
  if (pc != b.pc)
    return false;

  if ((!loopInProcess.isNull()) || !b.loopInProcess.isNull()) {
    llvm::errs() << "-- Loop in process: merge unsupported "
                 << "for loop invariant analysis.\n";
    return false;
  }

  // XXX is it even possible for these to differ? does it matter? probably
  // implies difference in object states?
  if (symbolics != b.symbolics)
    return false;

  {
    std::vector<StackFrame>::const_iterator itA = stack.begin();
    std::vector<StackFrame>::const_iterator itB = b.stack.begin();
    while (itA != stack.end() && itB != b.stack.end()) {
      // XXX vaargs?
      if (itA->caller != itB->caller || itA->kf != itB->kf)
        return false;
      ++itA;
      ++itB;
    }
    if (itA != stack.end() || itB != b.stack.end())
      return false;
  }

  std::set<ref<Expr>> aConstraints(constraints.begin(), constraints.end());
  std::set<ref<Expr>> bConstraints(b.constraints.begin(), b.constraints.end());
  std::set<ref<Expr>> commonConstraints, aSuffix, bSuffix;
  std::set_intersection(
      aConstraints.begin(), aConstraints.end(), bConstraints.begin(),
      bConstraints.end(),
      std::inserter(commonConstraints, commonConstraints.begin()));
  std::set_difference(aConstraints.begin(), aConstraints.end(),
                      commonConstraints.begin(), commonConstraints.end(),
                      std::inserter(aSuffix, aSuffix.end()));
  std::set_difference(bConstraints.begin(), bConstraints.end(),
                      commonConstraints.begin(), commonConstraints.end(),
                      std::inserter(bSuffix, bSuffix.end()));
  if (DebugLogStateMerge) {
    llvm::errs() << "\tconstraint prefix: [";
    for (std::set<ref<Expr>>::iterator it = commonConstraints.begin(),
                                       ie = commonConstraints.end();
         it != ie; ++it)
      llvm::errs() << *it << ", ";
    llvm::errs() << "]\n";
    llvm::errs() << "\tA suffix: [";
    for (std::set<ref<Expr>>::iterator it = aSuffix.begin(), ie = aSuffix.end();
         it != ie; ++it)
      llvm::errs() << *it << ", ";
    llvm::errs() << "]\n";
    llvm::errs() << "\tB suffix: [";
    for (std::set<ref<Expr>>::iterator it = bSuffix.begin(), ie = bSuffix.end();
         it != ie; ++it)
      llvm::errs() << *it << ", ";
    llvm::errs() << "]\n";
  }

  // We cannot merge if addresses would resolve differently in the
  // states. This means:
  //
  // 1. Any objects created since the branch in either object must
  // have been free'd.
  //
  // 2. We cannot have free'd any pre-existing object in one state
  // and not the other

  if (DebugLogStateMerge) {
    llvm::errs() << "\tchecking object states\n";
    llvm::errs() << "A: " << addressSpace.objects << "\n";
    llvm::errs() << "B: " << b.addressSpace.objects << "\n";
  }

  std::set<const MemoryObject *> mutated;
  MemoryMap::iterator ai = addressSpace.objects.begin();
  MemoryMap::iterator bi = b.addressSpace.objects.begin();
  MemoryMap::iterator ae = addressSpace.objects.end();
  MemoryMap::iterator be = b.addressSpace.objects.end();
  for (; ai != ae && bi != be; ++ai, ++bi) {
    if (ai->first != bi->first) {
      if (DebugLogStateMerge) {
        if (ai->first < bi->first) {
          llvm::errs() << "\t\tB misses binding for: " << ai->first->id << "\n";
        } else {
          llvm::errs() << "\t\tA misses binding for: " << bi->first->id << "\n";
        }
      }
      return false;
    }
    if (ai->second.get() != bi->second.get()) {
      if (DebugLogStateMerge)
        llvm::errs() << "\t\tmutated: " << ai->first->id << "\n";
      mutated.insert(ai->first);
    }
  }
  if (ai != ae || bi != be) {
    if (DebugLogStateMerge)
      llvm::errs() << "\t\tmappings differ\n";
    return false;
  }

  // merge stack

  ref<Expr> inA = ConstantExpr::alloc(1, Expr::Bool);
  ref<Expr> inB = ConstantExpr::alloc(1, Expr::Bool);
  for (std::set<ref<Expr>>::iterator it = aSuffix.begin(), ie = aSuffix.end();
       it != ie; ++it)
    inA = AndExpr::create(inA, *it);
  for (std::set<ref<Expr>>::iterator it = bSuffix.begin(), ie = bSuffix.end();
       it != ie; ++it)
    inB = AndExpr::create(inB, *it);

  // XXX should we have a preference as to which predicate to use?
  // it seems like it can make a difference, even though logically
  // they must contradict each other and so inA => !inB

  std::vector<StackFrame>::iterator itA = stack.begin();
  std::vector<StackFrame>::const_iterator itB = b.stack.begin();
  for (; itA != stack.end(); ++itA, ++itB) {
    StackFrame &af = *itA;
    const StackFrame &bf = *itB;
    for (unsigned i = 0; i < af.kf->numRegisters; i++) {
      ref<Expr> &av = af.locals[i].value;
      const ref<Expr> &bv = bf.locals[i].value;
      if (!av || !bv) {
        // if one is null then by implication (we are at same pc)
        // we cannot reuse this local, so just ignore
      } else {
        av = SelectExpr::create(inA, av, bv);
      }
    }
  }

  for (std::set<const MemoryObject *>::iterator it = mutated.begin(),
                                                ie = mutated.end();
       it != ie; ++it) {
    const MemoryObject *mo = *it;
    const ObjectState *os = addressSpace.findObject(mo);
    const ObjectState *otherOS = b.addressSpace.findObject(mo);
    assert(os && !os->readOnly &&
           "objects mutated but not writable in merging state");
    assert(otherOS);
    assert(os->isAccessible() && otherOS->isAccessible() &&
           "Merging of inaccessible objects is not supported.");

    ObjectState *wos = addressSpace.getWriteable(mo, os);
    for (unsigned i = 0; i < mo->size; i++) {
      ref<Expr> av = wos->read8(i);
      ref<Expr> bv = otherOS->read8(i);
      wos->write(i, SelectExpr::create(inA, av, bv));
    }
  }

  constraints = ConstraintSet();

  ConstraintManager m(constraints);
  for (const auto &constraint : commonConstraints)
    m.addConstraint(constraint);
  m.addConstraint(OrExpr::create(inA, inB));

  return true;
}

void ExecutionState::dumpStack(llvm::raw_ostream &out) const {
  unsigned idx = 0;
  const KInstruction *target = prevPC;
  for (ExecutionState::stack_ty::const_reverse_iterator it = stack.rbegin(),
                                                        ie = stack.rend();
       it != ie; ++it) {
    const StackFrame &sf = *it;
    Function *f = sf.kf->function;
    const InstructionInfo &ii = *target->info;
    out << "\t#" << idx++;
    std::stringstream AssStream;
    AssStream << std::setw(8) << std::setfill('0') << ii.assemblyLine;
    out << AssStream.str();
    out << " in " << f->getName().str() << " (";
    // Yawn, we could go up and print varargs if we wanted to.
    unsigned index = 0;
    for (Function::arg_iterator ai = f->arg_begin(), ae = f->arg_end();
         ai != ae; ++ai) {
      if (ai != f->arg_begin())
        out << ", ";

      out << ai->getName().str();
      // XXX should go through function
      ref<Expr> value = sf.locals[sf.kf->getArgRegister(index++)].value;
      if (isa_and_nonnull<ConstantExpr>(value))
        out << "=" << value;
    }
    out << ")";
    if (ii.file != "")
      out << " at " << ii.file << ":" << ii.line;
    out << "\n";
    target = sf.caller;
  }
}

bool symbolSetsIntersect(const SymbolSet &a, const SymbolSet &b) {
  if (a.size() > b.size())
    return symbolSetsIntersect(b, a);
  for (SymbolSet::const_iterator i = a.begin(), e = a.end(); i != e; ++i) {
    if (b.count(*i))
      return true;
  }
  return false;
}

std::vector<ref<Expr>>
ExecutionState::relevantConstraints(SymbolSet symbols) const {
  std::vector<ref<Expr>> ret;
  llvm::SmallPtrSet<Expr *, 32> insertedConstraints;
  bool newSymbols = false;
  do {
    newSymbols = false;
    for (ConstraintSet::const_iterator ci = constraints.begin(),
                                       cEnd = constraints.end();
         ci != cEnd; ++ci) {
      if (insertedConstraints.count((*ci).get()))
        continue;
      SymbolSet constrainedSymbols = GetExprSymbols::visit(*ci);
      if (symbolSetsIntersect(constrainedSymbols, symbols)) {
        for (SymbolSet::const_iterator csi = constrainedSymbols.begin(),
                                       cse = constrainedSymbols.end();
             csi != cse; ++csi) {
          bool inserted = std::get<1>(symbols.insert(*csi));
          newSymbols = newSymbols || inserted;
        }
        symbols.insert(constrainedSymbols.begin(), constrainedSymbols.end());
        ret.push_back(*ci);
        insertedConstraints.insert((*ci).get());
      }
    }
  } while (newSymbols);
  return ret;
}

bool ExecutionState::isAccessibleAddr(ref<Expr> addr) const {
  ObjectPair op;
  ref<klee::ConstantExpr> address = cast<klee::ConstantExpr>(addr);
  bool success = addressSpace.resolveOne(address, op);
  assert(success && "Unknown pointer result!");
  const ObjectState *os = op.second;
  return os->isAccessible();
}

ref<Expr>
ExecutionState::readMemoryChunk(ref<Expr> addr, Expr::Width width,
                                bool circumventInaccessibility) const {
  ObjectPair op;
  ref<klee::ConstantExpr> address = cast<klee::ConstantExpr>(addr);
  bool success = addressSpace.resolveOne(address, op);
  assert(success && "Unknown pointer result!");
  const MemoryObject *mo = op.first;
  const ObjectState *os = op.second;
  // FIXME: assume inbounds.
  ref<Expr> offset = mo->getOffsetExpr(address);
  assert(0 < width && "Can not read a zero-length value.");
  return os->read(offset, width, circumventInaccessibility);
}

void ExecutionState::traceRet() {
  if (callPath.empty() || callPath.back().returned ||
      callPath.back().f != stack.back().kf->function) {
    if (!callPath.empty()) {
      SymbolSet symbols = callPath.back().computeRetSymbolSet();
      relevantSymbols.insert(symbols.begin(), symbols.end());
    }
    callPath.push_back(CallInfo());
    callPath.back().callPlace = stack.back().caller->inst->getDebugLoc();
    callPath.back().f = stack.back().kf->function;
    callPath.back().returned = false;
    std::vector<ref<Expr>> constrs = relevantConstraints(relevantSymbols);
    callPath.back().callContext.insert(callPath.back().callContext.end(),
                                       constrs.begin(), constrs.end());
  }
}

void ExecutionState::traceRetPtr(Expr::Width width, bool tracePointee) {
  traceRet();
  RetVal *ret = &callPath.back().ret;
  ret->isPtr = true;
  ret->pointee.doTraceValueIn = tracePointee;
  ret->pointee.doTraceValueOut = tracePointee;
  ret->pointee.width = width;
}

void ExecutionState::traceArgValue(ref<Expr> val, std::string name) {
  traceRet();
  callPath.back().args.push_back(CallArg());
  CallArg *argInfo = &callPath.back().args.back();
  argInfo->expr = val;
  argInfo->isPtr = false;
  argInfo->name = name;
  std::vector<ref<Expr>> constrs =
      relevantConstraints(GetExprSymbols::visit(val));
  callPath.back().callContext.insert(callPath.back().callContext.end(),
                                     constrs.begin(), constrs.end());
}

void ExecutionState::traceArgPtr(ref<Expr> arg, Expr::Width width,
                                 std::string name, std::string type,
                                 bool tracePointeeIn, bool tracePointeeOut) {
  traceArgValue(arg, name);
  CallArg *argInfo = &callPath.back().args.back();
  argInfo->isPtr = true;
  argInfo->pointee.width = width;
  argInfo->pointee.type = type;
  argInfo->funPtr = NULL;
  argInfo->pointee.doTraceValueIn = tracePointeeIn;
  argInfo->pointee.doTraceValueOut = tracePointeeOut;
  SymbolSet symbols = GetExprSymbols::visit(arg);
  if (tracePointeeIn) {
    argInfo->pointee.inVal = readMemoryChunk(arg, width, true);
    SymbolSet indirectSymbols = GetExprSymbols::visit(argInfo->pointee.inVal);
    symbols.insert(indirectSymbols.begin(), indirectSymbols.end());
  }
  std::vector<ref<Expr>> constrs = relevantConstraints(symbols);
  callPath.back().callContext.insert(callPath.back().callContext.end(),
                                     constrs.begin(), constrs.end());
}

void ExecutionState::traceArgFunPtr(ref<Expr> arg, std::string name) {
  traceArgValue(arg, name);
  CallArg *argInfo = &callPath.back().args.back();
  argInfo->isPtr = true;
  ref<klee::ConstantExpr> address = cast<klee::ConstantExpr>(arg);
  argInfo->funPtr = (Function *)address->getZExtValue();
}

void ExecutionState::traceArgPtrField(ref<Expr> arg, int offset,
                                      Expr::Width width, std::string name,
                                      bool doTraceValueIn,
                                      bool doTraceValueOut) {
  assert(!callPath.empty() && callPath.back().f == stack.back().kf->function &&
         "Must trace the function first to trace a particular field.");
  CallArg *argInfo = callPath.back().getCallArgPtrp(arg);
  assert(argInfo != 0 &&
         "Must first trace the pointer arg to trace a particular field.");
  assert(argInfo->pointee.width > 0 && "Cannot fit a field into zero bytes.");
  assert((argInfo->pointee.doTraceValueIn || !doTraceValueIn) &&
         "Must trace the whole pointee to trace a single field.");
  assert((argInfo->pointee.doTraceValueOut || !doTraceValueOut) &&
         "Must trace the whole pointee to trace a single field.");
  assert(argInfo->pointee.fields.count(offset) == 0 && "Conflicting field.");
  FieldDescr descr;
  descr.width = width;
  descr.name = name;
  size_t base = (cast<ConstantExpr>(arg))->getZExtValue();
  if (doTraceValueIn) {
    ref<ConstantExpr> addrExpr =
        ConstantExpr::alloc(base + offset, sizeof(size_t) * 8);
    descr.inVal = readMemoryChunk(addrExpr, width, true);
  }
  descr.addr = base + offset;
  descr.doTraceValueIn = doTraceValueIn;
  descr.doTraceValueOut = doTraceValueOut;
  argInfo->pointee.fields[offset] = descr;
}

void ExecutionState::traceArgPtrNestedField(ref<Expr> arg, int base_offset,
                                            int offset, Expr::Width width,
                                            std::string name, bool trace_in,
                                            bool trace_out) {
  assert(!callPath.empty() && callPath.back().f == stack.back().kf->function &&
         "Must trace the function first to trace a particular field.");
  CallArg *argInfo = callPath.back().getCallArgPtrp(arg);
  assert(argInfo != 0 &&
         "Must first trace the pointer arg to trace a particular field.");
  assert(argInfo->pointee.width > 0 && "Cannot fit a field into zero bytes.");
  if (trace_in) {
    assert(argInfo->pointee.doTraceValueIn &&
           "Must trace the whole pointee to trace"
           " a single field.");
  }
  if (trace_out) {
    assert(argInfo->pointee.doTraceValueOut &&
           "Must trace the whole pointee to trace"
           " a single field.");
  }
  assert(argInfo->pointee.fields.count(base_offset) != 0 &&
         "Must first trace the field itself.");
  assert(argInfo->pointee.fields[base_offset].fields.count(offset) == 0 &&
         "Conflicting field.");
  FieldDescr descr;
  descr.width = width;
  descr.name = name;
  size_t base = (cast<ConstantExpr>(arg))->getZExtValue();
  if (trace_in) {
    ref<ConstantExpr> addrExpr =
        ConstantExpr::alloc(base + base_offset + offset, sizeof(size_t) * 8);
    descr.inVal = readMemoryChunk(addrExpr, width, true);
  }
  descr.addr = base + base_offset + offset;
  descr.doTraceValueIn = trace_in;
  descr.doTraceValueOut = trace_out;
  argInfo->pointee.fields[base_offset].fields[offset] = descr;
}

void ExecutionState::traceExtraPtrNestedField(size_t ptr, int base_offset,
                                              int offset, Expr::Width width,
                                              std::string name, bool trace_in,
                                              bool trace_out) {
  assert(!callPath.empty() && callPath.back().f == stack.back().kf->function &&
         "Must trace the function first to trace a particular field.");
  CallExtraPtr *extraPtr = &callPath.back().extraPtrs[ptr];
  assert(extraPtr != 0 &&
         "Must first trace the extra pointer to trace a particular field.");
  assert(extraPtr->pointee.width > (unsigned)offset + (unsigned)base_offset &&
         "Cannot fit a field into zero bytes.");
  assert(extraPtr->pointee.fields.count(base_offset) != 0 &&
         "Must first trace the field itself.");
  assert(extraPtr->pointee.fields[base_offset].fields.count(offset) == 0 &&
         "Conflicting field.");
  FieldDescr descr;
  descr.width = width;
  descr.name = name;
  size_t base = ptr;
  if (trace_in) {
    ref<ConstantExpr> addrExpr =
        ConstantExpr::alloc(base + base_offset + offset, sizeof(size_t) * 8);
    descr.inVal = readMemoryChunk(addrExpr, width, true);
  }
  descr.addr = base + base_offset + offset;
  descr.doTraceValueIn = trace_in;
  descr.doTraceValueOut = trace_out;
  extraPtr->pointee.fields[base_offset].fields[offset] = descr;
}

void ExecutionState::traceExtraPtrNestedNestedField(
    size_t ptr, int base_base_offset, int base_offset, int offset,
    Expr::Width width, std::string name, bool trace_in, bool trace_out) {
  assert(!callPath.empty() && callPath.back().f == stack.back().kf->function &&
         "Must trace the function first to trace a particular field.");
  CallExtraPtr *extraPtr = &callPath.back().extraPtrs[ptr];
  assert(extraPtr != 0 &&
         "Must first trace the extra pointer to trace a particular field.");
  assert(extraPtr->pointee.width > (unsigned)offset + (unsigned)base_offset +
                                       (unsigned)base_base_offset &&
         "Cannot fit a field into zero bytes.");
  assert(extraPtr->pointee.fields.count(base_base_offset) != 0 &&
         "Must first trace the base base field itself.");
  assert(extraPtr->pointee.fields[base_base_offset].fields.count(base_offset) !=
             0 &&
         "Must first trace the base field itself.");
  assert(extraPtr->pointee.fields[base_base_offset]
                 .fields[base_offset]
                 .fields.count(offset) == 0 &&
         "Conflicting field.");
  FieldDescr descr;
  descr.width = width;
  descr.name = name;
  size_t base = ptr;
  descr.addr = base + base_base_offset + base_offset + offset;
  if (trace_in) {
    ref<ConstantExpr> addrExpr =
        ConstantExpr::alloc(descr.addr, sizeof(size_t) * 8);
    descr.inVal = readMemoryChunk(addrExpr, width, true);
  }
  descr.doTraceValueIn = trace_in;
  descr.doTraceValueOut = trace_out;
  extraPtr->pointee.fields[base_base_offset]
      .fields[base_offset]
      .fields[offset] = descr;
}

void ExecutionState::traceExtraValue(ref<Expr> val, std::string name,
                                     std::string prefix) {
  traceRet();
  callPath.back().extraVals.push_back(CallExtraVal());
  CallExtraVal *extraVal = &callPath.back().extraVals.back();
  extraVal->expr = val;
  extraVal->name = name;
  extraVal->prefix = prefix;
}

void ExecutionState::traceExtraPtr(size_t ptr, Expr::Width width,
                                   std::string name, std::string type,
                                   std::string prefix, bool trace_in,
                                   bool trace_out) {
  traceRet();
  callPath.back().extraPtrs.insert(
      std::pair<const size_t, CallExtraPtr>(ptr, CallExtraPtr()));
  CallExtraPtr *extraPtr = &callPath.back().extraPtrs[ptr];
  extraPtr->ptr = ptr;
  extraPtr->name = name;
  extraPtr->prefix = prefix;
  extraPtr->pointee.width = width;
  extraPtr->pointee.type = type;
  extraPtr->pointee.doTraceValueIn = trace_in;
  extraPtr->pointee.doTraceValueOut = trace_out;
  extraPtr->accessibleIn =
      trace_in &&
      isAccessibleAddr(ConstantExpr::alloc(ptr, 8 * sizeof(size_t)));
  extraPtr->accessibleOut = trace_out;

  SymbolSet indirectSymbols;
  if (trace_in) {
    extraPtr->pointee.inVal = ConstraintManager::simplifyExpr(constraints, readMemoryChunk(
        ConstantExpr::alloc(ptr, sizeof(size_t) * 8), width, true));
    indirectSymbols = GetExprSymbols::visit(extraPtr->pointee.inVal);
  }
  std::vector<ref<Expr>> constrs = relevantConstraints(indirectSymbols);
  callPath.back().callContext.insert(callPath.back().callContext.end(),
                                     constrs.begin(), constrs.end());
}

void ExecutionState::traceExtraPtrField(size_t ptr, int offset,
                                        Expr::Width width, std::string name,
                                        bool trace_in, bool trace_out) {
  assert(!callPath.empty() && callPath.back().f == stack.back().kf->function &&
         "Must trace the function first to trace a particular field.");
  CallExtraPtr *extraPtr = &callPath.back().extraPtrs[ptr];
  assert(extraPtr->pointee.width > 0 && "Cannot fit a field into zero bytes.");
  assert(extraPtr->pointee.fields.count(offset) == 0 && "Conflicting field.");
  FieldDescr descr;
  descr.width = width;
  descr.name = name;
  size_t base = ptr;
  if (trace_in) {
    ref<ConstantExpr> addrExpr =
        ConstantExpr::alloc(base + offset, sizeof(size_t) * 8);
    descr.inVal = readMemoryChunk(addrExpr, width, true);
  }
  descr.addr = base + offset;
  descr.doTraceValueIn = trace_in;
  descr.doTraceValueOut = trace_out;
  extraPtr->pointee.fields[offset] = descr;
}

void ExecutionState::traceExtraFPtr(ref<Expr> ptr, Expr::Width width,
                                    std::string name, std::string type,
                                    std::string prefix, bool trace_in,
                                    bool trace_out) {
  traceRet();
  callPath.back().extraFPtrs.push_back(CallExtraFPtr());
  CallExtraFPtr *extraFPtr = &callPath.back().extraFPtrs.back();
  extraFPtr->ptr = (cast<ConstantExpr>(ptr))->getZExtValue();
  extraFPtr->name = name;
  extraFPtr->prefix = prefix;
  extraFPtr->width = width;
  ref<klee::ConstantExpr> address = cast<klee::ConstantExpr>(ptr);
  extraFPtr->funPtr = (Function *)address->getZExtValue();

  uint32_t fn_id = 0;
  llvm::StringRef fn_name = extraFPtr->funPtr->getName();
  if (name == "map_hash") {
    if (fn_name == "FlowId_hash" || fn_name == "fw_flow_hash")
      fn_id = 1;
    else if (fn_name == "ether_addr_hash")
      fn_id = 2;
    else if (fn_name == "lb_flow_hash")
      fn_id = 3;
    else if (fn_name == "lb_ip_hash")
      fn_id = 4;
    else if (fn_name == "policer_flow_hash")
      fn_id = 5;
    else if (fn_name == "natasha_flow_hash")
      fn_id = 5;
  } else if (name == "map_key_eq") {
    if (fn_name == "FlowId_eq" || fn_name == "fw_flow_eq")
      fn_id = 1;
    else if (fn_name == "ether_addr_eq")
      fn_id = 2;
    else if (fn_name == "lb_flow_equality")
      fn_id = 3;
    else if (fn_name == "lb_ip_equality")
      fn_id = 4;
    else if (fn_name == "policer_flow_eq")
      fn_id = 5;
    else if (fn_name == "natasha_flow_eq")
      fn_id = 5;
  }
  assert(fn_id > 0 && "Unknown hash function");
  klee::ExprBuilder *Builder = klee::createDefaultExprBuilder();
  extraFPtr->inVal = extraFPtr->outVal =
      Builder->Constant(fn_id, extraFPtr->width);
}

void ExecutionState::traceRetPtrField(int offset, Expr::Width width,
                                      std::string name, bool doTraceValue) {
  assert(!callPath.empty() && callPath.back().f == stack.back().kf->function &&
         "Must trace the function first to trace a particular field.");
  RetVal *ret = &callPath.back().ret;
  assert(ret->isPtr && "Only a pointer can have fields traced.");
  assert(ret->pointee.width > 0 &&
         "Cannot fit a field in zero sized mem chunk.");
  assert(ret->pointee.doTraceValueIn && "Must trace the whole pointee to trace"
                                        " a single field.");
  assert(ret->pointee.fields.count(offset) == 0 && "Fields conflict");
  FieldDescr descr;
  descr.width = width;
  descr.name = name;
  descr.addr = 0;
  descr.doTraceValueIn = doTraceValue;
  descr.doTraceValueOut = doTraceValue;
  ret->pointee.fields[offset] = descr;
}

void ExecutionState::traceRetPtrNestedField(int base_offset, int offset,
                                            Expr::Width width,
                                            std::string name) {
  assert(!callPath.empty() && callPath.back().f == stack.back().kf->function &&
         "Must trace the function first to trace a particular field.");
  RetVal *ret = &callPath.back().ret;
  assert(ret->isPtr && "Only a pointer can have fields traced.");
  assert(ret->pointee.width > 0 &&
         "Cannot fit a field in zero sized mem chunk.");
  assert(ret->pointee.doTraceValueIn && "Must trace the whole pointee to trace"
                                        " a single field.");
  assert(ret->pointee.fields.count(base_offset) != 0 &&
         "Must first trace the base field.");
  assert(ret->pointee.fields[base_offset].fields.count(offset) == 0 &&
         "Fields conflict");
  FieldDescr descr;
  descr.width = width;
  descr.name = name;
  descr.addr = 0;
  descr.doTraceValueIn = true;
  descr.doTraceValueOut = true;
  ret->pointee.fields[base_offset].fields[offset] = descr;
}

void ExecutionState::recordRetConstraints(CallInfo *info) const {
  assert(!callPath.empty() && info->f == stack.back().kf->function);
  SymbolSet symbols = info->computeRetSymbolSet();

  std::vector<ref<Expr>> constrs = relevantConstraints(symbols);
  info->returnContext.insert(info->returnContext.end(), constrs.begin(),
                             constrs.end());
}

ExecutionState *ExecutionState::finishLoopRound(KFunction *kf) {
  bool analysisFinished = false;
  ExecutionState *nextRoundState =
      loopInProcess->nextRoundState(&analysisFinished);
  // TODO: here analysis may never finish, e.g. if inside the analysed loop
  // there is an infinite loop somewhere. If we cound detect infinite loops,
  // we may ensure that the analysis finishes. Otherwise, Klee will be blocked
  // on this loop, even though there are paths that avoid that infinite loop,
  // and the search heuristic may guide execution away.
  if (analysisFinished) {
    kf->insert(loopInProcess->getLoop(), loopInProcess->getChangedBytes(),
               loopInProcess->getEntryState());
    LOG_LA("[" << loopInProcess->getLoop()
               << "]analysis finished, loop inserted");
  }
  return nextRoundState;
}

void ExecutionState::loopRepetition(const llvm::Loop *dstLoop,
                                    TimingSolver *solver, bool *terminate) {
  // TODO: detect infinite loops.
  LOG_LA("Loop repetition");
  if (!loopInProcess.isNull()) {
    if (loopInProcess->getLoop() == dstLoop) {
      LOG_LA("[" << loopInProcess->getLoop() << "]The loop is in process.")
      loopInProcess->updateChangedObjects(*this, solver);
      LOG_LA("refcount: " << loopInProcess->_refCount.getCount());
      LOG_LA("Terminating the loop-repeating state.");
      *terminate = true;
      return;
    }
  }
  if (analysedLoops.count(dstLoop)) {
    LOG_LA("terminate loop repeating state for an analyzed loop.");
    *terminate = true;
    return;
  }
  *terminate = false;
}

void ExecutionState::loopEnter(const llvm::Loop *dstLoop) {
  LOG_LA("Loop enter");
  // Get ready for the next analysis run, which may have
  // different starting conditions.
  LOG_LA("Remove the loop from the analyzed set - prepare"
         " to repeat the analysis.");
  analysedLoops = analysedLoops.remove(dstLoop);
  /// Remember the initial state for this loop header in
  /// case ther is an klee_induce_invariants call following.
  LOG_LA("store the loop-head entering state,"
         " just in case.");
  auto copied_state = std::make_unique<ExecutionState>(*this);
  executionStateForLoopInProcess.swap(copied_state);

  // this special executionState does not need open merge stack. 
  if (UseIncompleteMerge) {
    for (auto cur_mergehandler: executionStateForLoopInProcess->openMergeStack){
      cur_mergehandler->removeOpenState(executionStateForLoopInProcess.get());
    }
    executionStateForLoopInProcess->openMergeStack.clear();
  }
  executionStateForLoopInProcess->loopInProcess = 0;
}

void ExecutionState::loopExit(const llvm::Loop *srcLoop, bool *terminate) {
  LOG_LA("Loop exit in state " << (void *)this);
  if (!loopInProcess.isNull()) {
    if (loopInProcess->getLoop() == srcLoop) {
      LOG_LA("[" << loopInProcess->getLoop() << "]The loop is in process");
      LOG_LA("Terminating loop-escaping state.");
      *terminate = true;
      return;
    }
  } else {
    // no invariant analysis. Clean up.
    if (executionStateForLoopInProcess) {
      assert (executionStateForLoopInProcess->loopInProcess.isNull());
      ExecutionState *replacement = nullptr;
      executionStateForLoopInProcess->terminateState(&replacement);
      executionStateForLoopInProcess = nullptr;
    }
  }
  *terminate = false;
}

void ExecutionState::updateLoopAnalysisForBlockTransfer(BasicBlock *dst,
                                                        BasicBlock *src,
                                                        TimingSolver *solver,
                                                        bool *terminate) {
  KFunction *kf = stack.back().kf;
  const llvm::Loop *dstLoop = kf->loopInfo.getLoopFor(dst);
  const llvm::Loop *srcLoop = kf->loopInfo.getLoopFor(src);
  *terminate = false;
  if (srcLoop) {
    if (dstLoop) {
      if (srcLoop == dstLoop) {
        if (dst == dstLoop->getHeader()) {
          // Loop repetition.
          loopRepetition(dstLoop, solver, terminate);
        } else {
          // In-loop transition
        }
      } else if (srcLoop->contains(dstLoop)) {
        // Nested loop enter
        assert(dstLoop->getHeader() == dst);
        loopEnter(dstLoop);
      } else if (dstLoop->contains(srcLoop)) {
        // Nested loop exit
        loopExit(srcLoop, terminate);
        if (dst == dstLoop->getHeader()) {
          // Loop repetition.
          loopRepetition(dstLoop, solver, terminate);
        }
      } else {
        // Transition from one loop to another
        // Loop enter + Loop exit
        assert(dstLoop->getHeader() == dst);
        loopEnter(dstLoop);
        loopExit(srcLoop, terminate);
      }
    } else {
      // Loop exit
      loopExit(srcLoop, terminate);
    }
  } else {
    if (dstLoop) {
      // Loop enter
      assert(dstLoop->getHeader() == dst);
      loopEnter(dstLoop);
    } else {
      // Out-of-loop transition.
    }
  }
}

void ExecutionState::terminateState(ExecutionState **replace) {
  LOG_LA("Terminating: " << (void *)this);
  if (!loopInProcess.isNull()) {
    *replace = finishLoopRound(stack.back().kf);
    loopInProcess = 0;
    LOG_LA(" - replacing with: " << (void *)(*replace));
  }
}

void ExecutionState::startInvariantSearch() {
  KInstruction *inst = prevPC;
  llvm::Instruction *linst = inst->inst;
  assert(linst);
  LOG_LA(linst->getOpcodeName());
  BasicBlock *bb = linst->getParent();
  assert(bb);

  KFunction *kf = stack.back().kf;
  const KFunction::LInfo &loopInfo = kf->loopInfo;

  const llvm::Loop *loop = loopInfo.getLoopFor(bb);

  LOG_LA("loop being analysed:" << loop);
  if ((loopInProcess.isNull() || loopInProcess->getLoop() != loop) &&
      analysedLoops.count(loop) == 0) {
    LOG_LA("Start search for loop invariants.");

    assert(executionStateForLoopInProcess &&
           "The initial execution state must have been stored at the entrance"
           " of the loop header block.");

    assert(!loopInfo.empty());
    assert(loop &&
           "The klee_induce_invariants must be placed into the condition"
           " of a loop.");
    assert(loopInfo.isLoopHeader(bb) &&
           "The klee_induce_invariants must be placed into the condition"
           " of a loop.");
    
    assert(!UseIncompleteMerge &&
           "Loop analysis is not supported with incomplete merge.");

    std::unique_ptr<ExecutionState> loop_state(nullptr);
    loop_state.swap(executionStateForLoopInProcess);
    loopInProcess =
        new LoopInProcess(loop, std::move(loop_state), loopInProcess);
    executionStateForLoopInProcess = nullptr;
  } else {
    LOG_LA("Already analysed, or being analysed at this very moment");
  }
}

void ExecutionState::induceInvariantsForThisLoop(KInstruction *target) {
  startInvariantSearch();

  // The return value of the intrinsic function call.
  stack.back().locals[target->dest].value =
      ConstantExpr::create(0xffffffff, Expr::Int32);
}

bool FieldDescr::eq(const FieldDescr &other) const {
  bool self_eq = width == other.width && name == other.name &&
                 type == other.type && doTraceValueIn == other.doTraceValueIn &&
                 doTraceValueOut == other.doTraceValueOut &&
                 (!doTraceValueIn ||
                  (inVal.isNull() ? other.inVal.isNull()
                                  : (!other.inVal.isNull()) &&
                                        0 == inVal->compare(*other.inVal))) &&
                 (!doTraceValueOut ||
                  (outVal.isNull() ? other.outVal.isNull()
                                   : (!other.outVal.isNull()) &&
                                         0 == outVal->compare(*other.outVal)));
  if (!self_eq)
    return false;

  if (!doTraceValueIn && !doTraceValueOut) {
    return true;
  }

  std::map<int, FieldDescr>::const_iterator i = fields.begin(),
                                            e = fields.end();
  for (; i != e; ++i) {
    std::map<int, FieldDescr>::const_iterator it = other.fields.find(i->first);
    if (it == other.fields.end() || !it->second.eq(i->second))
      return false;
  }
  return true;
}

bool FieldDescr::sameInvocationValue(const FieldDescr &other) const {
  bool self_same =
      width == other.width && name == other.name && type == other.type &&
      doTraceValueIn == other.doTraceValueIn &&
      (!doTraceValueIn ||
       (inVal.isNull()
            ? other.inVal.isNull()
            : (!other.inVal.isNull()) && 0 == inVal->compare(*other.inVal)));
  if (!self_same)
    return false;
  if (!doTraceValueIn)
    return true;
  std::map<int, FieldDescr>::const_iterator i = fields.begin(),
                                            e = fields.end();
  for (; i != e; ++i) {
    std::map<int, FieldDescr>::const_iterator it = other.fields.find(i->first);
    if (it == other.fields.end() || !it->second.sameInvocationValue(i->second))
      return false;
  }
  return true;
}

bool CallArg::eq(const CallArg &other) const {
  if (expr.isNull()) {
    if (!other.expr.isNull())
      return false;
  } else {
    if (other.expr.isNull())
      return false;
    if (0 != expr->compare(*other.expr))
      return false;
  }
  if (isPtr) {
    if (!other.isPtr)
      return false;
    if (!pointee.eq(other.pointee))
      return false;
  } else {
    if (other.isPtr)
      return false;
  }
  return true;
}

// Essentially same as eq, but doe not compare the output states.
bool CallArg::sameInvocationValue(const CallArg &other) const {
  if (expr.isNull()) {
    if (!other.expr.isNull())
      return false;
  } else {
    if (other.expr.isNull())
      return false;
    if (0 != expr->compare(*other.expr))
      return false;
  }
  if (isPtr) {
    if (!other.isPtr)
      return false;
    if (!pointee.sameInvocationValue(other.pointee))
      return false;
  } else {
    if (other.isPtr)
      return false;
  }
  return true;
}

bool RetVal::eq(const RetVal &other) const {
  if (expr.isNull()) {
    if (!other.expr.isNull())
      return false;
  } else {
    if (other.expr.isNull())
      return false;
    if (0 != expr->compare(*other.expr))
      return false;
  }
  if (isPtr) {
    if (!other.isPtr)
      return false;
    if (!pointee.eq(other.pointee))
      return false;
  } else {
    if (other.isPtr)
      return false;
  }
  return true;
}

bool CallExtraPtr::eq(const CallExtraPtr &other) const {
  if (ptr != other.ptr)
    return false;
  if (accessibleIn != other.accessibleIn)
    return false;
  if (accessibleOut != other.accessibleOut)
    return false;
  if (!pointee.eq(other.pointee))
    return false;
  if (name != other.name)
    return false;
  return true;
}

// Essentially same as eq, but doe not compare the output states.
bool CallExtraPtr::sameInvocationValue(const CallExtraPtr &other) const {
  if (ptr != other.ptr)
    return false;
  if (accessibleIn != other.accessibleIn)
    return false;
  if (!pointee.sameInvocationValue(other.pointee))
    return false;
  if (name != other.name)
    return false;
  return true;
}

CallArg *CallInfo::getCallArgPtrp(ref<Expr> ptr) {
  for (unsigned i = 0; i < args.size(); ++i) {
    CallArg *cur = &args[i];
    if (cur->isPtr && 0 == cur->expr->compare(*ptr))
      return cur;
  }
  return 0;
}

bool equalContexts(const std::vector<ref<Expr>> &a,
                   const std::vector<ref<Expr>> &b) {
  // TODO: Structural-only comparison here, ideally we'd ask the solver about it
  if (a.size() != b.size())
    return false;
  for (unsigned i = 0; i < a.size(); ++i) {
    bool notFound = true;
    for (unsigned j = 0; j < b.size(); ++j) {
      if ((*a[i]).compare(*b[j]) == 0) {
        notFound = false;
        break;
      }
    }
    if (notFound)
      return false;
  }
  for (unsigned i = 0; i < b.size(); ++i) {
    bool notFound = true;
    for (unsigned j = 0; j < a.size(); ++j) {
      if ((*a[i]).compare(*b[j]) == 0) {
        notFound = false;
        break;
      }
    }
    if (notFound)
      return false;
  }
  return true;
}

bool CallInfo::eq(const CallInfo &other) const {

  if (args.size() != other.args.size()) {
    return false;
  }

  for (unsigned i = 0; i < args.size(); ++i) {
    if (!args[i].eq(other.args[i])) {
      return false;
    }
  }

  return f == other.f && ret.eq(other.ret) &&
         equalContexts(callContext, other.callContext) &&
         equalContexts(returnContext, other.returnContext) &&
         returned == other.returned;
}

bool CallInfo::sameInvocation(const CallInfo *other) const {
  // TODO: compare assumptions as well.
  if (args.size() != other->args.size())
    return false;
  // HACK: Not comparing extra ptrs for now, since depending on result value an
  // extra ptr may exist or not
  // if (extraPtrs.size() != other->extraPtrs.size()) return false;
  if (f != other->f)
    return false;
  for (unsigned i = 0; i < args.size(); ++i) {
    if (!args[i].sameInvocationValue(other->args[i]))
      return false;
  }
  /*std::map<size_t, CallExtraPtr>::const_iterator i = extraPtrs.begin(),
    e = extraPtrs.end();
  for (; i != e; ++i) {
    std::map<size_t, CallExtraPtr>::const_iterator it =
      other->extraPtrs.find(i->first);
    if (it == other->extraPtrs.end() ||
        !it->second.sameInvocationValue(i->second)) return false;
  }*/
  return equalContexts(callContext, other->callContext);
}

SymbolSet CallInfo::computeRetSymbolSet() const {
  assert(returned && "incomplete");
  SymbolSet symbols;
  if (!ret.expr.isNull()) {
    symbols = GetExprSymbols::visit(ret.expr);
  }
  if (ret.isPtr && ret.funPtr == NULL && ret.pointee.doTraceValueOut) {
    SymbolSet ptrSymbols = GetExprSymbols::visit(ret.pointee.outVal);
    symbols.insert(ptrSymbols.begin(), ptrSymbols.end());
  }
  for (unsigned i = 0; i < args.size(); ++i) {
    if (args[i].isPtr && args[i].funPtr == NULL &&
        args[i].pointee.doTraceValueOut) {
      SymbolSet argSymbols = GetExprSymbols::visit(args[i].pointee.outVal);
      symbols.insert(argSymbols.begin(), argSymbols.end());
    }
  }
  for (std::map<size_t, CallExtraPtr>::const_iterator i = extraPtrs.begin(),
                                                      e = extraPtrs.end();
       i != e; ++i) {
    if (!i->second.pointee.doTraceValueOut)
      continue;
    SymbolSet indirectSymbols = GetExprSymbols::visit(i->second.pointee.outVal);
    symbols.insert(indirectSymbols.begin(), indirectSymbols.end());
  }
  return symbols;
}

LoopInProcess::LoopInProcess(const llvm::Loop *_loop,
                             std::unique_ptr<ExecutionState> &&_headerState,
                             const ref<LoopInProcess> &_outer)
    : outer(_outer), loop(_loop), restartState(std::move(_headerState)),
      lastRoundUpdated(false) {
  // TODO: this can not belong here. It has nothing to do with execution state,
  // nor with ptree node.
  restartState->ptreeNode = 0;
}

LoopInProcess::~LoopInProcess() {
  for (std::map<const MemoryObject *, BitArray *>::iterator
           i = changedBytes.begin(),
           e = changedBytes.end();
       i != e; ++i) {
    delete i->second;
  }
  assert(restartState);
}

unsigned countBitsSet(const BitArray *arr, unsigned size) {
  unsigned rez = 0;
  for (unsigned i = 0; i < size; ++i) {
    if (arr->get(i))
      ++rez;
  }
  return rez;
}

ExecutionState *LoopInProcess::makeRestartState() {
  auto newState = new ExecutionState(*restartState);
  newState->setID();
  LOG_LA("Making restart state " << (void *)newState << " from "
                                 << (void *)restartState.get());
  for (std::map<const MemoryObject *, BitArray *>::iterator
           i = changedBytes.begin(),
           e = changedBytes.end();
       i != e; ++i) {
    const MemoryObject *mo = i->first;
    const BitArray *bytes = i->second;
    if (mo->allocSite) {
      LOG_LA(" Forgetting: [" << countBitsSet(bytes, mo->size) << "/"
                              << mo->size << "]" << *mo->allocSite);
    } else {
      LOG_LA(" Forgetting something.\n");
    }
    const ObjectState *os = newState->addressSpace.findObject(mo);
    assert(os != 0 && "changedObjects must contain only existing objects.");
    assert(!os->readOnly && "Read only object can not have been changed");
    ObjectState *wos;
    bool wasInaccessible = !os->isAccessible();
    if (wasInaccessible) {
      wos = newState->addressSpace.allowAccess(mo, os);
    } else {
      wos = newState->addressSpace.getWriteable(mo, os);
    }
    const Array *array = wos->forgetThese(bytes);
    if (wasInaccessible) {
      wos->forbidAccessWithLastMessage();
    }

    auto havoc_info = newState->havocs.find(mo);
    if (havoc_info == newState->havocs.end() &&
        !restartState->condoneUndeclaredHavocs) {
      printf("Unexpected memory location being havoced.\n");
      assert(0 && "Possible havoc location must have been predelcared");
    }

    if (havoc_info != newState->havocs.end()) {
      // Remember the generated value for later reporting in the ktest file.
      havoc_info->second.value = array;
      havoc_info->second.havoced = true;
      havoc_info->second.mask = BitArray(*bytes, bytes->size());
      LOG_LA("Adding havoc here: " << havoc_info->second.name
                                   << " in: " << (void *)newState);
    }

    // Do not record this symbol, as it was not generated with
    // klee_make_symbolic.
    // newState->symbolics.push_back(std::make_pair(mo, array));
  }
  if (lastRoundUpdated) {
    LOG_LA("[" << loop
               << "]Some more objects were changed."
                  " repeat the loop.");
    lastRoundUpdated = false;
    // This works, because refCount is the internal field.
    newState->loopInProcess = this;
  } else {
    LOG_LA("[" << loop
               << "]Nothing else changed."
                  " Restart loop "
                  " in the normal mode.");
    newState->loopInProcess = outer;
    newState->analysedLoops = newState->analysedLoops.insert(loop);
  }
  return newState;
}

void LoopInProcess::updateChangedObjects(const ExecutionState &current,
                                         TimingSolver *solver) {
  bool updated = updateDiffMask(&changedBytes, restartState->addressSpace,
                                current, solver);
  if (updated)
    lastRoundUpdated = true;
}

ExecutionState *LoopInProcess::nextRoundState(bool *analysisFinished) {
  if (_refCount.getCount() == 1) {
    // The last state in the round.
    if (!lastRoundUpdated) {
      LOG_LA("[" << loop
                 << "]Fixpoint reached. Time to"
                    " restart the iteration in the normal mode.");
      *analysisFinished = true;
    } else {
      *analysisFinished = false;
    }
    // Order is important; makeRestartState clears the
    // lastRoundUpdated flag.
    LOG_LA("[" << loop
               << "]Schedule a fresh copy of the"
                  " restart state for the loop");
    return makeRestartState();
  }
  *analysisFinished = false;
  return 0;
}

void ExecutionState::dumpConstraints() const {
  const char *digits[10] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
  static int cnt = 0;
  ++cnt;
  std::string fname = "constraints";

  int tmp = cnt;
  while (0 < tmp) {
    fname = digits[tmp % 10] + fname;
    tmp /= 10;
  }
  fname += ".txt";
  std::error_code Error;
  llvm::raw_fd_ostream file(StringRef(fname.c_str()), Error, llvm::sys::fs::F_None);
  if (Error) {
    printf("error opening file \"%s\".  KLEE may have run out of file "
           "descriptors: try to increase the maximum number of open file "
           "descriptors by using ulimit.",
           fname.c_str());
    return;
  }
  file << ";;-- Constraints --\n";
  for (ConstraintSet::const_iterator ci = constraints.begin(),
                                     cEnd = constraints.end();
       ci != cEnd; ++ci) {
    file << **ci << "\n";
  }
  // for (ConstraintSet::constraint_iterator ci = constraints.begin(),
  //        cEnd = constraints.end(); ci != cEnd; ++ci) {
  //   const ref<Expr> constraint = *ci;
  //   std::cout <<*constraint <<std::endl;
  //}
}

void ExecutionState::addConstraint(ref<Expr> e) {
  ConstraintManager c(constraints);
  c.addConstraint(e);
}
