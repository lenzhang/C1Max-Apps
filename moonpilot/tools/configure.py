#!/usr/bin/env python3
"""Sync only MoonPilot's private configuration, separately from public releases."""
import argparse,json,pathlib,subprocess,uuid
from urllib.parse import urlsplit
p=argparse.ArgumentParser()
p.add_argument('--serial',required=True)
p.add_argument('--file',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[2]/'config/moonpilot.local.json')
a=p.parse_args()
try:
    raw=a.file.read_bytes()
    if len(raw)>8192:raise ValueError('Configuration exceeds 8 KiB')
    j=json.loads(raw)
    if set(j)-{'vision','chat','asr','tts','spoken','host'}:raise ValueError('Unexpected configuration field')
    if type(j.get('spoken',True)) is not bool:raise ValueError('spoken must be a boolean')
    if 'host' in j:
        import ipaddress
        host=j['host']
        if set(host)-{'address','port','width','height','fps','bitrate','app_id'}:raise ValueError('Unexpected host field')
        if host.get('address'):ipaddress.ip_address(host['address'])
        for key,lo,hi,default in [('port',1,65530,47989),('fps',5,30,15),('bitrate',250,4000,1500),('app_id',0,2147483647,0)]:
            value=host.get(key,default)
            if type(value) is not int or not lo<=value<=hi:raise ValueError('Invalid host parameter')
        if (host.get('width',640),host.get('height',360)) not in [(512,288),(640,360),(800,450)]:raise ValueError('Invalid stream dimensions')
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
adb=['adb','-s',a.serial];base='/storage/apps/data/moonpilot';temp=base+'/settings-'+uuid.uuid4().hex+'.tmp'
def shell(command):return subprocess.check_output(adb+['shell',command],text=True).replace('\r','').strip()
# Never race the application's own setting save or change a running session.
if shell('pidof c1max-moonpilot || true'):raise SystemExit('Exit MoonPilot before syncing its settings')
if shell('mkdir -p '+base+' && chmod 700 '+base+' && echo READY')!='READY':raise SystemExit('Could not prepare private directory')
try:
    subprocess.run(adb+['push',str(a.file),temp],check=True)
    if shell('chmod 600 '+temp+' && mv '+temp+' '+base+'/settings.json && echo SAVED')!='SAVED':raise RuntimeError('Settings activation failed')
finally:shell('rm -f '+temp)
print('MoonPilot private settings saved (0600); restart the app to load them.')
