#include "LoopAnalysis.h"

#include "../Core/ExecutionState.h"
#include "klee/Support/ErrorHandling.h"

#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DebugInfoMetadata.h"

using namespace llvm;
using namespace klee;

std::string __attribute__((weak)) numToStr(long long n) {
    std::stringstream ss;
    ss << n;
    return ss.str();
  }

bool klee::updateDiffMask(StateByteMask *mask, const AddressSpace &refValues,
                          const ExecutionState &state, TimingSolver *solver) {
  bool updated = false;
  for (MemoryMap::iterator i = refValues.objects.begin(),
                           e = refValues.objects.end();
       i != e; ++i) {
    const MemoryObject *obj = i->first;
    const ObjectState *refOs = i->second.get();
    const ObjectState *os = state.addressSpace.findObject(obj);
    if (refOs == os)
      continue;
    if (refOs->isAccessible() != os->isAccessible()) {
      std::string inacc_msg;
      if (refOs->isAccessible()) {
        inacc_msg = "cand " + os->inaccessible_message;
      } else {
        inacc_msg = "ref " + refOs->inaccessible_message;
      }
      printf("No support for accessibility alternation "
             "between loop iterations. Inaccessibility reason: %s\n",
             inacc_msg.c_str());
      exit(1);
    }
    assert(refOs->isAccessible() == os->isAccessible() &&
           "No support for accessibility alteration "
           "between loop iterations.");
    std::pair<std::map<const MemoryObject *, BitArray *>::iterator, bool>
        insRez =
            mask->insert(std::pair<const MemoryObject *, BitArray *>(obj, 0));

    if (state.havocs.find(obj) == state.havocs.end() &&
        !state.condoneUndeclaredHavocs) {
      ref<Expr> firstByteRef = refOs->read8(0, true);
      ref<Expr> firstByte = os->read8(0, true);
      fprintf(stderr, "First byte before: ");
      fflush(stderr);
      firstByteRef->dump();
      fprintf(stderr, "First byte after: ");
      fflush(stderr);
      firstByte->dump();
      fprintf(stderr, "Type: ");
      fflush(stderr);
      obj->allocSite->getType()->dump();
      fprintf(stderr, "\n");
      std::string metadata;
      if (isa<llvm::Instruction>(obj->allocSite)) {
        const llvm::Instruction *inst =
            dyn_cast<llvm::Instruction>(obj->allocSite);
        if (const DebugLoc &location = inst->getDebugLoc()) {
          if (const DILocation *loc = location.get()) {
            metadata = loc->getDirectory().str() + "/" +
                       loc->getFilename().str() + ":" +
                       numToStr(loc->getLine());
          }
        } else {
          metadata = "(unknown)";
        }
      } else {
        metadata = "(not an instruciton)";
      }
      klee_error("Unexpected memory location changed its value during "
                 "invariant analysis:\n"
                 "  name: %s\n  location: %s\n"
                 "  local: %s\n  global: %s\n"
                 "  fixed: %s\n  size: %u\n"
                 "  address: 0x%lx\n  metadata: %s",
                 obj->name.c_str(), obj->allocSite->getName().str().c_str(),
                 obj->isLocal ? "true" : "false",
                 obj->isGlobal ? "true" : "false",
                 obj->isFixed ? "true" : "false", obj->size, obj->address,
                 metadata.c_str());
    }

    if (insRez.second)
      insRez.first->second = new BitArray(obj->size);
    BitArray *bytes = insRez.first->second;
    assert(bytes != 0);
    unsigned size = obj->size;
    for (unsigned j = 0; j < size; ++j) {
      if (bytes->get(j))
        continue;
      ref<Expr> refVal = refOs->read8(j, true);
      ref<Expr> val = os->read8(j, true);
      if (0 != refVal->compare(*val)) {
        // So: this byte was not diferent on the previous round,
        // it also differs structuraly now. It is time to make
        // sure it can be really different.

        solver->setTimeout(
            time::Span("1")); // TODO: determine a correct argument here.
        bool mayDiffer = true;
        bool solverRes = solver->mayBeFalse(state.constraints, EqExpr::create(refVal, val),
                                            /*&*/ mayDiffer, state.queryMetaData);
        solver->setTimeout(time::Span());
        // assert(solverRes &&
        //       "Solver failed in computing whether a byte changed or not.");
        if (solverRes && mayDiffer) {
          bytes->set(j);
          updated = true;
        }
      }
    }
  }
  return updated;
}
