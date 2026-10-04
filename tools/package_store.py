#!/usr/bin/env python3
"""Create one immutable package per optional app from a verified device payload."""
import argparse,hashlib,json,pathlib,struct
root=pathlib.Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--tag',required=True)
parser.add_argument('--output',type=pathlib.Path,default=root/'.build/store-release')
args=parser.parse_args()
import re
if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,100}',args.tag):parser.error('Invalid release tag')
payload=root/'.build/device'
for line in (payload/'SHA256SUMS').read_text().splitlines():
    checksum,name=line.split('  ',1)
    if hashlib.sha256((payload/name).read_bytes()).hexdigest()!=checksum:raise SystemExit('Unverified payload: '+name)
labels={}
for line in (root/'launcher/apps.txt').read_text().splitlines():
    if line.startswith('#') or not line:continue
    label,exe,*rest=line.split('|');ident=pathlib.Path(exe).parent.name
    if ident in labels:continue
    labels[ident]=label
entries=[];args.output.mkdir(parents=True,exist_ok=True)
for app in json.loads((payload/'catalog.json').read_text())['apps']:
    ident=app['id']
    if ident=='launcher':continue
    files=[];contents=[]
    for source in sorted((payload/ident).rglob('*')):
        if source.is_symlink():raise SystemExit('No symlinks in app bundles')
        if not source.is_file():continue
        data=source.read_bytes();files.append({'path':str(source.relative_to(payload)),'size':len(data),'sha256':hashlib.sha256(data).hexdigest(),'mode':0o755 if source.stat().st_mode&0o111 else 0o644});contents.append(data)
    for source in sorted((payload/'shared/licenses').glob('*')):
        if not source.is_file():continue
        data=source.read_bytes();files.append({'path':ident+'/licenses/shared-'+source.name,'size':len(data),'sha256':hashlib.sha256(data).hexdigest(),'mode':0o644});contents.append(data)
    icon=payload/'launcher/icons'/('updates.bgra' if ident=='appstore' else ident+'.bgra')
    if icon.is_file():
        data=icon.read_bytes();files.append({'path':ident+'/icon.bgra','size':len(data),'sha256':hashlib.sha256(data).hexdigest(),'mode':0o644});contents.append(data)
    manifest={**app,'runtime':1,'files':files};header=json.dumps(manifest,ensure_ascii=False,separators=(',',':')).encode()
    name=f'{ident}-{app["version"]}-{app["revision"][:12]}.c1pkg';path=args.output/name
    with path.open('wb') as f:
        f.write(b'C1PKG01\n');f.write(struct.pack('<I',len(header)));f.write(header)
        for data in contents:f.write(data)
    entries.append({**app,'title':labels.get(ident,ident),'description':labels.get(ident,ident)+' · C1 Max 原生应用','url':f'https://github.com/zhuzhe1983/C1Max-Apps/releases/download/{args.tag}/{name}','size':path.stat().st_size,'unpacked':sum(len(c) for c in contents),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
catalog={'schema':1,'platform':'c1max-mipsel-linux','runtime':1,'release':args.tag,'apps':entries}
(args.output/'catalog.json').write_text(json.dumps(catalog,ensure_ascii=False,indent=2)+'\n')
print(f'Created {len(entries)} independent app packages in {args.output}')
