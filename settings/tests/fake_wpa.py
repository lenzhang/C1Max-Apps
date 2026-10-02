#!/usr/bin/env python3
import json,os,sys
from pathlib import Path
p=Path(os.environ['C1_SETTINGS_TEST_STATE']);s=json.loads(p.read_text());a=sys.argv[3:];s['commands'].append(a);c=a[0];out='OK';n=s['networks']
if c=='list_networks':
 out='FAIL' if s['mode']=='read-fail' else 'network id / ssid / bssid / flags\n'+'\n'.join(i+'\t'+v['ssid']+'\tany\t'+('[DISABLED]' if v['disabled'] else '')+('[CURRENT]' if s['current']==i else '') for i,v in n.items())
elif c=='status':out='wpa_state=COMPLETED\nid='+s['current']+'\nip_address=192.0.2.1' if s['current'] else 'wpa_state=DISCONNECTED'
elif c=='signal_poll':out='RSSI=-40'
elif c=='add_network':out=str(max(map(int,n),default=-1)+1);n[out]={'ssid':'','disabled':True}
elif c=='get_network':out=str(n[a[1]].get(a[2],'FAIL'))
elif c=='set_network':
 if s['mode']=='setup-fail' and a[2]=='psk':out='FAIL'
 else:n[a[1]][a[2]]=bytes.fromhex(a[3]).decode() if a[2]=='ssid' else a[3]
elif c=='select_network':
 for i,v in n.items():v['disabled']=i!=a[1]
 s['current']=a[1]
 if s['mode']=='select-fail' and a[1] not in ('0','1'):out='FAIL'
elif c in ('enable_network','disable_network'):n[a[1]]['disabled']=c=='disable_network'
elif c=='remove_network':
 n.pop(a[1]);s['current']='' if s['current']==a[1] else s['current']
elif c=='disconnect':s['current']=''
elif c=='save_config':
 out='FAIL' if s['mode']=='save-fail' else 'OK';s['saves']+=out=='OK'
else:raise SystemExit('Unexpected command: '+c)
p.write_text(json.dumps(s));print(out)
