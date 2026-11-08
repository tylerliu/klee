#!/usr/bin/env python3
"""Read-only verification of PIX merge ancestry, source identity and provenance."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
META = ('PIX-PORT.md', 'docs/pix-port-audit/')

def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args])

def metadata(path):
    return path == META[0] or path.startswith(META[1])

def tree(ref):
    entries = {}
    for row in git('ls-tree', '-rz', ref).split(b'\0'):
        if not row:
            continue
        head, path = row.split(b'\t', 1)
        mode, kind, oid = head.decode().split()
        entries[path.decode()] = {'mode': mode, 'blob': oid}
    return entries

def fingerprint(ref):
    rows = [r for r in git('ls-tree', '-rz', ref).split(b'\0')
            if r and not metadata(r.split(b'\t', 1)[1].decode())]
    return hashlib.sha256(b'\0'.join(sorted(rows)) + b'\0').hexdigest()

def require(condition, message):
    if not condition:
        raise SystemExit('FAIL: ' + message)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--docker-image', help='Also compare all port files with the locally available tested image')
args = parser.parse_args()
m = json.loads((ROOT / 'docs/pix-port-audit/manifest.json').read_text())
require(m['format'] == 1, 'unsupported audit format')
require(git('merge-base', m['donor'], m['upstream']).decode().strip() == m['merge_base'], 'merge base differs')
merges = [r.split() for r in git('rev-list', '--parents', '--first-parent', 'HEAD').decode().splitlines()]
merge = next((r[0] for r in merges if r[1:] == [m['donor'], m['upstream']]), None)
require(merge is not None, 'missing merge with donor first parent and upstream v3.3 second parent')
require(fingerprint(merge) == m['code_tree_sha256'], 'merge source differs from tested code')
base, donor, upstream, accepted = [tree(ref) for ref in (m['merge_base'], m['donor'], m['upstream'], merge)]
# Keep the sealed merge immutable. Each follow-up is bound to exact before/after
# Git blobs and an explicit path class; no unrecorded source changes are allowed.
expected = {p: entry for p, entry in accepted.items() if not metadata(p)}
followup_changes = {}
compiler_changes = {}
runtime_changes = {}
asan_changes = {}
for filename in ('build-followup.json', 'compiler-followup.json', 'runtime-followup.json', 'asan-followup.json'):
    followup_path = ROOT / 'docs/pix-port-audit' / filename
    if not followup_path.exists():
        continue
    followup = json.loads(followup_path.read_text())
    require(followup['format'] == 1 and followup['base_merge'] == merge,
            f'{filename}: invalid followup receipt')
    for path, change in followup['changes'].items():
        if filename == 'build-followup.json':
            require(path.startswith('scripts/build/') and path.endswith('.inc')
                    and '..' not in path.split('/'), 'followup outside build fragments')
            followup_changes[path] = change
        elif filename == 'compiler-followup.json':
            require(path in ('lib/Module/KModule.cpp', 'lib/Module/LoopAnalysis.cpp'),
                    'followup outside the two compiler fixes')
            compiler_changes[path] = change
        elif filename == 'runtime-followup.json':
            require(path in ('lib/Core/ExecutionState.cpp', 'lib/Core/Executor.cpp',
                             'test/Feature/DebugLogFunctionCalls.c'),
                    'followup outside the runtime fixes and their regression test')
            runtime_changes[path] = change
        else:
            require(path in ('include/klee/ADT/BitArray.h', 'unittests/CMakeLists.txt',
                             'unittests/BitArray/CMakeLists.txt', 'unittests/BitArray/BitArrayTest.cpp'),
                    'followup outside BitArray ownership fix and its unit tests')
            asan_changes[path] = change
        require(change['before'] == expected.get(path), f'{path}: invalid followup base')
        if change['before'] is None:
            require(path in ('test/Feature/DebugLogFunctionCalls.c',
                             'unittests/BitArray/CMakeLists.txt', 'unittests/BitArray/BitArrayTest.cpp') and
                    change['after']['mode'] == '100644', 'unexpected followup addition')
        else:
            require(change['after']['mode'] == change['before']['mode'],
                    f'{path}: followup changed file mode')
        expected[path] = change['after']
current = {p: entry for p, entry in tree('HEAD').items() if not metadata(p)}
require(current == expected, 'HEAD differs from sealed merge plus exact recorded followups')
for item in m['conflicts']:
    path = item['path']
    for key, source in [('base', base), ('donor', donor), ('upstream', upstream)]:
        require(item[key] == source.get(path), f'{path}: incorrect {key} blob or mode')
    if not metadata(path):
        require(item['accepted'] == accepted.get(path), f'{path}: resolution differs')
for item in m['overridden_automatic_results']:
    if not metadata(item['path']):
        require(item['accepted'] == accepted.get(item['path']), f"{item['path']}: automatic-result override differs")
require(not git('status', '--porcelain'), 'worktree has uncommitted changes')
print(f'PASS: merge {merge[:12]} has the exact PIX donor and upstream v3.3 parents')
print(f"PASS: sealed merge fingerprint matches tested port; only {META} are audit metadata")
if followup_changes:
    print(f'PASS: HEAD contains exactly {len(followup_changes)} recorded build-fragment changes')
if compiler_changes:
    print(f'PASS: HEAD contains exactly {len(compiler_changes)} recorded compiler-fix source changes')
if runtime_changes:
    print(f'PASS: HEAD contains exactly {len(runtime_changes)} recorded runtime/test changes')
if asan_changes:
    print(f'PASS: HEAD contains exactly {len(asan_changes)} recorded BitArray/test changes')
print(f"PASS: provenance records {len(m['conflicts'])} conflicts and {len(m['overridden_automatic_results'])} automatic-result overrides")
if args.docker_image:
    paths = sorted({p for p in set(upstream) | set(accepted)
                    if not metadata(p) and upstream.get(p) != accepted.get(p)}
                   | set(runtime_changes) | set(asan_changes))
    local = {p: [hashlib.sha256((ROOT / p).read_bytes()).hexdigest(), (ROOT / p).stat().st_mode & 0o111]
             if (ROOT / p).exists() else None for p in paths}
    code = """import hashlib,json,pathlib,sys
root=pathlib.Path('/opt/klee')
def digest(p):
 f=root/p
 return [hashlib.sha256(f.read_bytes()).hexdigest(), f.stat().st_mode & 0o111] if f.exists() else None
print(json.dumps({p:digest(p) for p in json.load(sys.stdin)}))
"""
    built = json.loads(subprocess.check_output(['docker', 'run', '--rm', '--network=none', '-i', args.docker_image,
                                               'python3', '-c', code], input=json.dumps(paths), text=True))
    require(local == built, 'port files differ from the Docker image')
    print(f'PASS: all {len(paths)} port files match Docker content and executable bits')
