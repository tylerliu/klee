# PIX extensions on KLEE 3.3

This branch merges upstream KLEE 3.3 into the original PIX fork. Its merge
commit has the original PIX donor as first parent and upstream v3.3 as second
parent. The resolutions import the already tested port tree from commit
`4053c7a8`; the production code is unchanged. That original patch-based branch
is retained as `codex/pix-klee33`.

See [the merge audit](docs/pix-port-audit/README.md) for the recorded conflict
stages, automatic-merge overrides, resolution rationale and read-only verifier.
The manifest fingerprints the tested code independently of commit history.

- Upstream base: `74f49f1d243f0e1692a00ac688a7b805b37c5646` (v3.3).
- PIX donor: `573acbe5080bc3d3b1860454d345f049e270a491` in `tylerliu/klee`.
- Donor/upstream merge base: `5719d2803e93252e5d4613f43afc7db0d72332f1` (v2.2).
- Applied PIX patch SHA256: `c08343ca72cfe67feb9e0293913d0f7cd1e3dd3eeb99570fcef948e9836757fe`.

The port preserves PIX's API/field/callback tracing, call and constraint output,
loop-invariant induction, accessibility/interception hooks, runtime aliases,
KTest v4 havoc serialization/native replay and performance-contract tools.
It retains modern upstream memory resolution, KDAlloc, execution-tree support
and solver ownership APIs. Callback tracing resolves synthetic function
addresses through a per-state map. Aggregate output is written before the
executor's expression-array cache is destroyed.

The associated PIX checkout contains the Docker recipes and regression harness
on branch `codex/pix-executor-upgrade`. From that PIX checkout, run:

```sh
./klee-test.sh build 25.11 18
./klee-test.sh test 25.11 18
```

That recipe builds LLVM/Clang 18.1.3, matching bitcode libc and KLEE, and DPDK
25.11. It uses explicit low KDAlloc heap/stack addresses for the hosted XDP
harness's 32-bit packet-address fields and selects the symbolic libc's pre-C23
strto* declarations for NF bitcode. Existing PIX CLI invocations must use the
new allocator options, supplied by the PIX recipe through `KLEE_ALLOC_FLAGS`.
Ordinary pointee tracing requires explicit sizes with LLVM opaque pointers.

The 144 port files were compared against the tested Docker image in both
content and executable bits. The regression run passed 43 unit tests and 423
system tests, with two expected failures and 29 unsupported tests. It also
passed NAT (21 witnesses), hosted XDP firewall (33 witnesses), seven native NF
builds, virtual NAT startup, contract ABI checks, tracing and native havoc
replay. These are functional/model tests; real accelerator execution, the
separate LLVM replay-tracer plugin, Intel Pin and full performance-tree
production remain unvalidated. Separate tracer projects and unrelated donor
CI/generated files are not part of this port; they remain in the donor branch.
