#!/usr/bin/env python3
"""Explicit Sunshine protocol fixture, never a substitute for a real stream test.

Exercises client certificates, PIN proof, pinned TLS, app selection, launch
parameters, cancellation and failure paths with a cryptographically independent
Python server. No desktop capture, HID or keyboard/mouse injection takes place.
"""
import datetime, hashlib, json, pathlib, ssl, subprocess, sys, tempfile, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit, parse_qs
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa, padding
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.x509.oid import NameOID

binary=pathlib.Path(sys.argv[1]).resolve()
private=tempfile.TemporaryDirectory(prefix='moonpilot-protocol-')
base=pathlib.Path(private.name)
key=rsa.generate_private_key(public_exponent=65537,key_size=2048)
name=x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,'Sunshine fixture')])
now=datetime.datetime.utcnow()
certificate=x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key()).serial_number(42).not_valid_before(now-datetime.timedelta(days=1)).not_valid_after(now+datetime.timedelta(days=1)).sign(key,hashes.SHA256())
pem=certificate.public_bytes(serialization.Encoding.PEM)
(base/'server.pem').write_bytes(pem)
(base/'server.key').write_bytes(key.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.TraditionalOpenSSL,serialization.NoEncryption()))
context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.load_cert_chain(str(base/'server.pem'),str(base/'server.key'))
sessions={};behavior={'busy':0,'tamper':False,'delay':False};launches=[]
def aes(data,key,decrypt=False):
    c=Cipher(algorithms.AES(key[:16]),modes.ECB())
    e=c.decryptor() if decrypt else c.encryptor()
    return e.update(data)+e.finalize()
class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def do_GET(self):
        try:self.handle_get()
        except (BrokenPipeError,ConnectionResetError,ssl.SSLError):pass
    def handle_get(self):
        u=urlsplit(self.path);q={k:v[0] for k,v in parse_qs(u.query).items()};identity=q.get('uniqueid','')
        assert len(identity)==16 and len(q['uuid'])==32
        s=sessions.setdefault(identity,{})
        secure=isinstance(self.connection,ssl.SSLSocket)
        body=''
        if behavior['delay']:time.sleep(3)
        if u.path=='/serverinfo':
            body=f'<hostname>Protocol fixture</hostname><appversion>7.1.431.0</appversion><GfeVersion>3.23.0.74</GfeVersion><HttpsPort>{https.server_port}</HttpsPort><PairStatus>{int(s.get("paired",False))}</PairStatus><currentgame>{behavior["busy"]}</currentgame><state>SUNSHINE_SERVER_{"BUSY" if behavior["busy"] else "FREE"}</state><ServerCodecModeSupport>1</ServerCodecModeSupport>'
        elif u.path=='/pair':
            if q.get('phrase')=='getservercert':
                client=bytes.fromhex(q['clientcert']);s['cert']=x509.load_pem_x509_certificate(client);s['aes']=hashlib.sha256(bytes.fromhex(q['salt'])+b'1234').digest()
                context.load_verify_locations(cadata=client.decode());context.verify_mode=ssl.CERT_REQUIRED
                body='<paired>1</paired><plaincert>'+pem.hex()+'</plaincert>'
            elif 'clientchallenge' in q:
                s['challenge']=aes(bytes.fromhex(q['clientchallenge']),s['aes'],True);s['server_challenge']=b'0123456789ABCDEF';s['server_secret']=b'FEDCBA9876543210'
                digest=hashlib.sha256(s['challenge']+certificate.signature+s['server_secret']).digest()
                if behavior['tamper']:digest=b'\0'*32
                body='<paired>1</paired><challengeresponse>'+aes(digest+s['server_challenge'],s['aes']).hex()+'</challengeresponse>'
            elif 'serverchallengeresp' in q:
                s['proof']=aes(bytes.fromhex(q['serverchallengeresp']),s['aes'],True)
                sig=key.sign(s['server_secret'],padding.PKCS1v15(),hashes.SHA256())
                body='<paired>1</paired><pairingsecret>'+(s['server_secret']+sig).hex()+'</pairingsecret>'
            elif 'clientpairingsecret' in q:
                value=bytes.fromhex(q['clientpairingsecret']);secret,sig=value[:16],value[16:]
                s['cert'].public_key().verify(sig,secret,padding.PKCS1v15(),hashes.SHA256())
                s['paired']=hashlib.sha256(s['server_challenge']+s['cert'].signature+secret).digest()==s['proof']
                body=f'<paired>{int(s["paired"])}</paired>'
            elif q.get('phrase')=='pairchallenge':
                assert secure and s['paired'];body='<paired>1</paired>'
        elif u.path=='/applist':
            assert secure and s['paired'];body='<App><ID>1</ID><AppTitle>Desktop</AppTitle></App><App><ID>2</ID><AppTitle>Game &amp; test</AppTitle></App>'
        elif u.path in ('/launch','/resume'):
            assert secure and s['paired'] and q['appid']=='1' and q['mode']=='640x360x15'
            assert len(bytes.fromhex(q['rikey']))==16 and 0<=int(q['rikeyid'])<=2**32-1
            assert q['sops']=='0' and q['localAudioPlayMode']=='1'
            launches.append(q);body='<gamesession>1</gamesession><sessionUrl0>rtsp://127.0.0.1:48010</sessionUrl0>'
        else:raise AssertionError('unexpected path '+u.path)
        out=('<root status_code="200">'+body+'</root>').encode()
        self.send_response(200);self.send_header('Content-Length',str(len(out)));self.end_headers();self.wfile.write(out)

http=ThreadingHTTPServer(('127.0.0.1',0),Handler);https=ThreadingHTTPServer(('127.0.0.1',0),Handler)
https.socket=context.wrap_socket(https.socket,server_side=True)
threads=[threading.Thread(target=s.serve_forever,daemon=True) for s in (http,https)]
for t in threads:t.start()
def run(mode,folder='ok',pin=None,success=True):
    args=[str(binary),mode,'127.0.0.1',str(http.server_port),str(base/folder)]
    if pin is not None:args.append(pin)
    p=subprocess.run(args,capture_output=True,text=True,timeout=15)
    assert (p.returncode==0)==success,(mode,p.stdout,p.stderr)
    return json.loads(p.stdout) if success and mode!='cancel' else p
try:
    assert not run('inspect')['paired']
    result=run('pair',pin='1234');assert result['paired'] and result['apps'][1]['name']=='Game & test'
    assert run('apps')['apps'][0]['id']==1
    assert run('launch')['width']==640 and len(launches)==1
    behavior['busy']=2;run('launch',success=False);assert len(launches)==1;behavior['busy']=0
    run('pair','wrong',pin='9876',success=False);assert not list((base/'wrong').rglob('server.pem'))
    behavior['tamper']=True;run('pair','tampered-proof',pin='1234',success=False);behavior['tamper']=False
    assert not list((base/'tampered-proof').rglob('server.pem'))
    for p in (base/'ok').rglob('*'):
        if p.is_file():assert p.stat().st_mode&0o077==0
    # Corrupt the saved pin: HTTPS must fail instead of falling back to HTTP.
    pinfile=next((base/'ok').rglob('server.pem'));original=pinfile.read_bytes()
    alternate=x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key()).serial_number(43).not_valid_before(now-datetime.timedelta(days=1)).not_valid_after(now+datetime.timedelta(days=1)).sign(key,hashes.SHA256())
    pinfile.write_bytes(alternate.public_bytes(serialization.Encoding.PEM));run('apps',success=False);pinfile.write_bytes(original)
    behavior['delay']=True;run('cancel');behavior['delay']=False
    print('PASS: PIN mutual proof, client TLS, pinned server certificate, app list, launch parameters, busy-host refusal, cancellation, private file modes')
finally:
    for s in (http,https):s.shutdown();s.server_close()
    for t in threads:t.join()
    private.cleanup()
