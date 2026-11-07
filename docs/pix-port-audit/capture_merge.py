import argparse, hashlib, json, subprocess
from pathlib import Path

def git(*args):
    return subprocess.check_output(['git',*args])
def tree(ref):
    out={}
    for row in git('ls-tree','-rz',ref).split(b'\0'):
        if not row: continue
        head,path=row.split(b'\t',1)
        mode,kind,oid=head.decode().split()
        out[path.decode()]={'mode':mode,'blob':oid}
    return out
upstream='74f49f1d243f0e1692a00ac688a7b805b37c5646'
donor='573acbe5080bc3d3b1860454d345f049e270a491'
tested='4053c7a82c60a2bb528690de1cd4955716042444'
if git('rev-parse','HEAD').decode().strip() != donor or git('rev-parse','MERGE_HEAD').decode().strip() != upstream:
    raise SystemExit('Run only in the unresolved donor-first, upstream-second merge')
accepted=tree(tested)
u=tree(upstream);d=tree(donor)
stages={}
for row in git('ls-files','--stage','-z').split(b'\0'):
    if not row: continue
    head,path=row.split(b'\t',1)
    mode,oid,stage=head.decode().split()
    stages.setdefault(path.decode(),{})[stage]={'mode':mode,'blob':oid}
conflicts=[];overrides=[]
for path in sorted(set(stages)|set(accepted)):
    entry=stages.get(path,{})
    resolved=accepted.get(path)
    choice='ported'
    if resolved==u.get(path): choice='upstream'
    elif resolved==d.get(path): choice='donor'
    if any(s in entry for s in ('1','2','3')):
        conflicts.append({'path':path,'base':entry.get('1'),'donor':entry.get('2'),'upstream':entry.get('3'),'accepted':resolved,'resolution':choice})
    elif entry.get('0')!=resolved:
        overrides.append({'path':path,'automatic':entry.get('0'),'accepted':resolved,'resolution':choice})
if not conflicts:
    raise SystemExit('Expected unresolved conflicts; capture before resolving the merge')
manifest={'format':1,'donor':donor,'upstream':upstream,'merge_base':git('merge-base',donor,upstream).decode().strip(),'tested_port_commit':tested,'patch_sha256':'c08343ca72cfe67feb9e0293913d0f7cd1e3dd3eeb99570fcef948e9836757fe','tested_docker_image':'sha256:d19d8517e36bcee8f4fe5455ad5b7d897e62fb73a9d78ae7d99c40cda5d07c4b','metadata_paths':['PIX-PORT.md','docs/pix-port-audit/'],'conflicts':conflicts,'overridden_automatic_results':overrides}
parser=argparse.ArgumentParser(description='Capture an unresolved donor/upstream merge before importing the tested resolution tree')
parser.add_argument('--output',required=True)
args=parser.parse_args()
rows=[r for r in git('ls-tree','-rz',tested).split(b'\0') if r and r.split(b'\t',1)[1] != b'PIX-PORT.md' and not r.split(b'\t',1)[1].startswith(b'docs/pix-port-audit/')]
manifest['code_tree_sha256']=hashlib.sha256(b'\0'.join(sorted(rows))+b'\0').hexdigest()
Path(args.output).write_text(json.dumps(manifest,indent=2)+'\n')
print(f'Recorded {len(conflicts)} conflicted paths and {len(overrides)} overrides of automatic results')
for x in conflicts:print(x['resolution'],x['path'])
print('Automatic result overrides:')
for x in overrides:print(x['resolution'],x['path'])
