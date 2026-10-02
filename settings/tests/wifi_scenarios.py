#!/usr/bin/env python3
"""Run in a Linux build container with the transaction-test executable as argv[1]."""
import copy,json,os,subprocess,sys,tempfile
from pathlib import Path
fake=Path(__file__).with_name('fake_wpa.py').resolve()
with tempfile.TemporaryDirectory(prefix='c1-settings-wifi-') as d:
 p=Path(d)/'state.json'
 for case in ['wrong-password','new-cancel','saved-cancel','edit-cancel','disconnected','setup-fail','select-fail','read-fail','success','save-fail']:
  initial={'networks':{'0':{'ssid':'Home','psk':'"original-working-key"','disabled':False,'scan_ssid':'1','priority':'5'},'1':{'ssid':'StayDisabled','psk':'"untouched-key"','disabled':True}},'current':'' if case=='disconnected' else '0','commands':[],'saves':0,'mode':case}
  p.write_text(json.dumps(initial));subprocess.run([sys.argv[1],case],check=True,env={**os.environ,'C1_SETTINGS_TEST_WPA':str(fake),'C1_SETTINGS_TEST_STATE':str(p)})
  result=json.loads(p.read_text())
  assert result['networks']['1']==initial['networks']['1'],case
  assert all(cmd[:2]!=['enable_network','all'] for cmd in result['commands'])
  if case in ('success','save-fail'):
   assert '0' not in result['networks'] and result['current']=='2'
   assert result['networks']['2']['psk']=='"wrong-password"'
   assert result['networks']['2']['scan_ssid']=='1' and result['networks']['2']['priority']=='5'
   assert result['saves']==(case=='success')
  else:
   assert result['networks']==initial['networks'],(case,result)
   assert result['current']==initial['current'],case
   assert result['saves']==0
  print('PASS',case,flush=True)
