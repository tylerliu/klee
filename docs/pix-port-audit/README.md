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
