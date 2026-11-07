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
parser.add_argument('--docker-image', help='Also compare all 144 port files with the locally available tested image')
args = parser.parse_args()
m = json.loads((ROOT / 'docs/pix-port-audit/manifest.json').read_text())
require(m['format'] == 1, 'unsupported audit format')
require(git('merge-base', m['donor'], m['upstream']).decode().strip() == m['merge_base'], 'merge base differs')
merges = [r.split() for r in git('rev-list', '--parents', '--first-parent', 'HEAD').decode().splitlines()]
merge = next((r[0] for r in merges if r[1:] == [m['donor'], m['upstream']]), None)
require(merge is not None, 'missing merge with donor first parent and upstream v3.3 second parent')
require(fingerprint(merge) == m['code_tree_sha256'], 'merge source differs from tested code')
require(fingerprint('HEAD') == m['code_tree_sha256'], 'HEAD source differs from tested code')
base, donor, upstream, accepted = [tree(ref) for ref in (m['merge_base'], m['donor'], m['upstream'], merge)]
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
print(f"PASS: source fingerprint matches the tested port; only {META} are excluded as audit metadata")
print(f"PASS: provenance records {len(m['conflicts'])} conflicts and {len(m['overridden_automatic_results'])} automatic-result overrides")
if args.docker_image:
    paths = sorted(p for p in set(upstream) | set(accepted)
                   if not metadata(p) and upstream.get(p) != accepted.get(p))
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
