//===-- LoopAnalysis.h ------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LOOP_ANALYSIS_H
#define LOOP_ANALYSIS_H

#include "klee/ADT/BitArray.h"
#include "../Core/AddressSpace.h"

namespace klee {
class MemoryObject;
class ExecutionState;
class TimingSolver;

/// A global bytemask for all the memory of a program.
using StateByteMask = std::map<const MemoryObject *, BitArray *>;

struct LoopEntryState {
  StateByteMask forgetMask;
  const AddressSpace addressSpace;

 LoopEntryState(const StateByteMask &_fmask,
                const AddressSpace &_aspace)
 :forgetMask(_fmask), addressSpace(_aspace)
  {}
 };

bool updateDiffMask(StateByteMask* mask,
                      const AddressSpace& refValues,
                      const ExecutionState& state,
                      TimingSolver* solver);

//#define DO_LOG_LOOP_ANALYSIS
#ifdef DO_LOG_LOOP_ANALYSIS
#define LOG_LA(expr)                                \
  llvm::errs() <<"[LA]" <<__FILE__ <<":" <<__LINE__ \
               <<": " << expr <<"\n";
#else//DO_LOG_LOOP_ANALYSIS
#define LOG_LA(expr)
#endif//DO_LOG_LOOP_ANALYSIS

}

#endif//LOOP_ANALYSIS_H
