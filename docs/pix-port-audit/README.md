# Auditing the PIX / KLEE 3.3 merge

## What happened

The first port was a single commit (`4053c7a8`) on upstream v3.3, retained on
`codex/pix-klee33`. It did not preserve the PIX donor as an ancestor. The
`codex/pix-klee33-merge` branch now contains an actual Git merge with these
parents, in order:

1. PIX donor `573acbe5080bc3d3b1860454d345f049e270a491`.
2. Upstream v3.3 `74f49f1d243f0e1692a00ac688a7b805b37c5646`.

The common ancestor is upstream v2.2,
`5719d2803e93252e5d4613f43afc7db0d72332f1`.

The merge was run with `git merge --no-commit --no-ff <upstream>`. Its unresolved
index was captured before resolution. The final resolution tree was imported
from the already tested port commit with `git read-tree --reset -u 4053c7a8`;
conflicts were not independently re-ported a second time. The merge then adds
only this audit directory and updates `PIX-PORT.md`. No `ours` merge strategy
or synthetic ancestry-only merge was used. Both original histories remain
ancestors; the original donor branch and patch-based port branch remain intact.

## Machine-readable evidence

`manifest.json` records all **22 conflicted paths**, their stage-1/base,
stage-2/PIX and stage-3/upstream blobs and modes, and the accepted blob/mode.
It also records **24 overrides of automatic merge results**, including intended
removals. Entries classified `upstream` select the upstream file or its absence;
`ported` means an explicit integrated result, rather than claiming either
parent already supplied it unchanged. `PIX-PORT.md` is an audit-only addition.

The manifest pins the tested commit, Docker image, original port patch SHA256,
and a SHA256 fingerprint of the production tree. The fingerprint hashes sorted,
NUL-separated `git ls-tree -rz` records, including paths, modes and blob IDs,
excluding only `PIX-PORT.md` and `docs/pix-port-audit/`. It works after cloning
this merge branch even if the older patch-based commit was not fetched.

Run the read-only audit:

```sh
python3 docs/pix-port-audit/verify.py
python3 docs/pix-port-audit/verify.py --docker-image pix-klee-test:llvm18-dpdk25.11
```

The first command checks merge parents, common ancestry, captured conflict
inputs/resolutions, accepted overrides, tested source identity and a clean
worktree. The second also checks the contents and executable bits of all 144
port files against the locally available tested Docker image. It does not
rerun compilation or execute NF tests. The earlier regression runs exercised
that identical production tree; their results are recorded in `PIX-PORT.md`
and the associated PIX checkout's `docker/executor/validation.md`.

Inspect the two histories and the actual PIX delta relative to modern upstream:

```sh
git log --graph --oneline --all -n 30
git show --no-patch --format=raw HEAD
git diff 74f49f1d243f0e1692a00ac688a7b805b37c5646 HEAD -- include lib runtime tools test
```

The donor-to-result diff includes all intervening upstream development, so the
upstream-to-result diff is the smaller review of the retained PIX extensions.

## Resolution rationale

| Paths | Resolution and reason |
| --- | --- |
| `include/klee/Module/KModule.h`, `lib/Module/KModule.cpp` | Keep modern callable/module interfaces and pass-manager support, add PIX loop analysis, and avoid constructing loop information for declarations. |
| `lib/Core/ExecutionState.{h,cpp}` | Keep modern allocator state, preferences, pointer-resolution caches and ownership. Carry PIX loop/havoc, alias, interception and trace state. Copy callback-address mappings and reused-symbol metadata across forks. |
| `lib/Core/Executor.{h,cpp}` | Integrate PIX handlers with modern call/memory APIs, termination types and execution trees. Resolve synthetic function addresses; require explicit pointee sizes for opaque pointers. Release old fixed mappings before rebuilding the memory manager. |
| `lib/Core/Memory.{h,cpp}` | Keep modern byte masks and memory interfaces, port PIX accessibility and havoc operations. The executor checks forbidden accesses on fast and multiple-resolution paths. |
| `lib/Core/SpecialFunctionHandler.{h,cpp}` | Retain the upstream handler set and add PIX intrinsics and handlers; adapt calls to current executor/state interfaces. |
| `lib/Module/IntrinsicCleaner.cpp` | Retain upstream LLVM 18 lowering and carry PIX's x86 special cases and fence treatment. These inherited modeling assumptions are not new instruction proofs. |
| `tools/klee/main.cpp` | Retain upstream option/test handling, including XML output. Restore PIX trace/constraint serializers and KTest havoc output. Emit aggregate expressions before destroying their owning executor. |
| `.github/workflows/build.yaml`, `.travis.yml`, `README.md`, `scripts/build/p-{klee,stp}-linux-ubuntu.inc`, `test/lit.cfg`, `test/Feature/ImpliedValue.c.failing`, `unittests/{CMakeLists.txt,Solver/CMakeLists.txt,TestMain.cpp}` | Select modern upstream build, documentation and testing infrastructure, including upstream deletion where applicable. |

The automatic-result overrides are equally deliberate:

- `TerminationTypes.h` retains the `inaccessible.err` identity in the modern
  termination framework; `perf-contracts.h` removes the hardcoded old C++ ABI;
  `LoopAnalysis.cpp` uses release-compatible LLVM type printing.
- Custom tool CMake files inherit modern LLVM/includes/definitions explicitly;
  solver factories use current ownership APIs. `ktest-gen` and `ktest-randgen`
  zero-initialize the extended KTest structure.
- Upstream `.gitignore` and build-script presence/absence are retained.
  Generated Python bytecode from the donor is excluded.
- Donor-only `trace-instructions/` and `trace-ir-instrs/` projects are excluded
  from this port, as they were from the tested patch. They remain in donor
  history. Intel Pin, the separate replay plugin and full performance-tree
  production are outside the validated scope; this merge does not silently
  establish their compatibility.

## Reconstructing the unresolved merge record

In a fresh disposable worktree based on the exact donor, merge the upstream
commit with the command above. Exit status 1 is expected for the conflicts.
Before resolving anything, run `capture_merge.py --output <file>` from this
audit directory while the current directory is that disposable worktree.
Compare its JSON with `manifest.json`. The original patch-based commit must
also be available for that reconstruction; fetch branch `codex/pix-klee33`
when it has been published, or use the local branch in this repository.
`capture_merge.py` checks HEAD and MERGE_HEAD before recording the index.
Git's merge algorithm/version may affect automatically merged blobs; the
recorded parent blobs and accepted production-tree fingerprint are fixed.

## Follow-up: differential ShellCheck fixes (2026-11-08)

The original merge and `manifest.json` remain immutable. `build-followup.json`
records exact before/after Git blob IDs and modes for 20 build fragments. The
verifier accepts precisely these changes in `scripts/build/*.inc` and continues
to reject unrecorded changes anywhere outside audit metadata. The executor,
runtime, CMake files, C/C++ tests and the 144 Docker port files are unchanged.

The supplied CI log had 53 warnings in its **NEEDS INSPECTION** section; the
preceding **Fixed defects** section describes resolved baseline diagnostics.
All 53 reported warnings are resolved. The warning-level ShellCheck 0.11.0 scan
of all build fragments and the new regression script decreases from 113 to 49
diagnostics, with no newly introduced diagnostics. The remaining diagnostics
predate this follow-up; this does not claim a globally clean repository scan.
No workflow severity or global ShellCheck exclusions were changed.

Individual SC2034 directives document variables consumed across dynamically
sourced component interfaces. Genuinely unused assignments were removed. Ubuntu,
Debian and Arch LLVM checks now use the supplied strict-mode argument; shared
mode probing still propagates command failure without storing an unused value.
LLVM configure/install functions stop on failed directory changes, and boolean
conversion failures are no longer hidden by `local` declarations.

Run the focused behavioral tests in the executor container:

```sh
docker run --rm --network=none \
  --mount "type=bind,source=$PWD,target=/src,readonly" \
  pix-klee-test:llvm18-dpdk25.11 \
  bash /src/docs/pix-port-audit/test-build-shell-fixes.sh
```

The tests cover relaxed/strict LLVM checks across all three distributions,
RTTI/assertion mismatches, shared-mode probe failures, mismatched LLVM versions,
failed configure directory changes and invalid boolean conversions. They use
mock tools and never install packages or build LLVM. ShellCheck was reproduced
with `koalaman/shellcheck:v0.11.0`, image digest recorded in the follow-up receipt.
A full executor regression was not repeated for this build-fragment-only fix;
the Docker comparison revalidates the original tested port files.

## Follow-up: LLVM dominator header and loop diagnostic formats (2026-11-08)

`compiler-followup.json` binds two further source changes to exact Git blobs.
The verifier permits only `lib/Module/KModule.cpp` and
`lib/Module/LoopAnalysis.cpp` for this receipt; all other source remains bound
to the sealed merge plus the earlier build-fragment receipt.

KFunction's loop analysis constructed `DominatorTreeBase<BasicBlock, false>`
without its defining header. The failure was reproduced with LLVM 15.0.7 and
GCC 13.3 in Docker. An explicit `llvm/IR/Dominators.h` include resolves it.
`DEBUG_TYPE` is defined after the includes to avoid LLVM 15's internal macro
collision. Loop analysis behavior is unchanged. Its memory-object diagnostic
now uses `%zu` for `size_t` and `PRIxPTR` for `uintptr_t`, with `<cinttypes>`.

Both corrected translation units compile against LLVM 15.0.7/GCC 13.3 and
LLVM 18.1.8/GCC 11.4. The original `%u` warning is reproduced as an error using
`-Werror=format`, and the fix passes that check. LLVM 18's baseline KModule
still compiled through transitive includes; this is a header compatibility
failure rather than evidence that the previously tested 18.1.3 build failed.

`test-compiler-headers.sh` records the translation-unit probe. It runs inside
Docker with the KLEE checkout mounted at `/src`, generated KLEE include files
at `/config/include`, and pre-fix copies from commit `301d07e8` at
`/config/before-KModule.cpp` and `/config/before-LoopAnalysis.cpp`. Arguments are
the LLVM install prefix and `yes`/`no` for an expected baseline KModule failure.
The probe adjusts generated LLVM version constants to the selected headers;
it does not substitute LLVM APIs or invoke native external functions from KLEE.

The final LLVM 18.1.3 executor is rebuilt as
`pix-klee-test:llvm18-compiler-fix`; its exact image ID and refreshed PIX patch
SHA256 are recorded in the receipt. Use this image for `verify.py --docker-image`
to compare all 144 port files after the two-file fix. The older image remains
an immutable baseline and intentionally differs in these two files.

The final rebuilt image passed the full PIX smoke suite: 43 unit tests, 423
system tests, two expected failures, 29 unsupported tests, NAT/XDP symbolic
execution (21/33 witnesses), seven native NF builds, virtual-port startup,
contract ABI, havoc replay and 124 clean accelerator witnesses. The refreshed
PIX patch also applies cleanly to the pinned upstream KLEE commit.

## Follow-up: assertions-disabled CI and C++ diagnostic output (2026-11-08)

The linked Linux **Asserts disabled** job built successfully but failed three
system tests: `LoppWithIncompleteMerge.c`, `nested_fail.cpp` and
`simple_exception_fail.cpp`. All three failures were reproduced locally with
assertions disabled and CI's libc++ / standard uclibc artifacts.

The loop incompatibility check was an `assert`, so release builds discarded it.
It is now an explicit `klee_error` before invariant processing. The existing
negative test keeps its original expectations and passes with assertions off.

The two C++ exceptions already terminated correctly. The unconditional PIX
function-call warnings interrupted libc++'s multi-part stderr diagnostic,
splitting its expected line. Those diagnostic warnings are now opt-in via
`--debug-log-function-calls`. The new `DebugLogFunctionCalls.c` test verifies a
quiet default and once-per-function logging when enabled. Structured call paths,
constraint trees, request events and performance-contract tracing are unchanged;
the existing exception test expectations have not been weakened.

`runtime-followup.json` records exact before/after blobs for the two runtime
files and the added regression test. The verifier permits only those paths and
compares all 145 port/test files against the updated Docker image. The original
sealed merge and the earlier follow-up receipts remain intact.

The C++ reproduction image enables `ENABLE_KLEE_LIBCXX`, `ENABLE_KLEE_EH_CXX`
and uses `ENABLE_KLEE_ASSERTS=OFF`. It contains the libc++ and uclibc archives
from the same GHCR artifact tags used by CI. The normal PIX validation image
retains assertions and its original minimal NF libc. Image identities, test
results and the synchronized patch SHA256 are recorded in the receipt.

Final assertions-disabled C++ results: 43 unit tests passed; 447 system tests
passed, two expected failures and six unsupported, with no failures. Lit used
four workers to match the container CPU limit. An initial oversubscribed run
used 48 workers and timed out the existing ten-second const-array performance
test; that test passed alone and in the complete four-worker run without any
test changes. The normal PIX smoke run passed 43 unit tests, 424 system tests,
two expected failures, 29 unsupported, and all NF/trace/replay/accelerator checks.
