#!/usr/bin/env python3
"""Sync only HIDPilot's private configuration, separately from public releases."""
import argparse,json,pathlib,subprocess,uuid
from urllib.parse import urlsplit
p=argparse.ArgumentParser()
p.add_argument('--serial',required=True)
p.add_argument('--file',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[2]/'config/hidpilot.local.json')
a=p.parse_args()
try:
    raw=a.file.read_bytes()
    if len(raw)>8192:raise ValueError('Configuration exceeds 8 KiB')
    j=json.loads(raw)
    if set(j)-{'vision','chat','asr','tts','spoken'}:raise ValueError('Unexpected configuration field')
    if type(j.get('spoken',True)) is not bool:raise ValueError('spoken must be a boolean')
    for name in ('vision','chat','asr','tts'):
        service=j[name]
        if set(service)-{'endpoint','model','token'}:raise ValueError('Unexpected service field')
        for key in ('endpoint','model','token'):
            value=service.get(key,'')
            if not isinstance(value,str) or len(value.encode())>(160 if key=='model' else 512) or any(c in value for c in '\r\n\x00'):raise ValueError('Invalid service value')
        url=service['endpoint']
        if url:
            u=urlsplit(url)
            if u.scheme not in ('http','https') or not u.hostname or u.username or u.password or u.query or u.fragment or any(c.isspace() for c in url):raise ValueError('Invalid service URL')
            u.port
        if url and not service.get('model'):raise ValueError('Configured services need a model name')
except (OSError,ValueError,KeyError,TypeError) as e:raise SystemExit('Invalid private settings: '+str(e))
adb=['adb','-s',a.serial];base='/storage/apps/data/hidpilot';temp=base+'/settings-'+uuid.uuid4().hex+'.tmp'
def shell(command):return subprocess.check_output(adb+['shell',command],text=True).replace('\r','').strip()
# Never race the application's own setting save or change a running session.
if shell('pidof c1max-hidpilot || true'):raise SystemExit('Exit HIDPilot before syncing its settings')
if shell('mkdir -p '+base+' && chmod 700 '+base+' && echo READY')!='READY':raise SystemExit('Could not prepare private directory')
try:
    subprocess.run(adb+['push',str(a.file),temp],check=True)
    if shell('chmod 600 '+temp+' && mv '+temp+' '+base+'/settings.json && echo SAVED')!='SAVED':raise RuntimeError('Settings activation failed')
finally:shell('rm -f '+temp)
print('HIDPilot private settings saved (0600); restart the app to load them.')
