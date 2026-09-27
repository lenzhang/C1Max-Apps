#!/usr/bin/env python3
"""Linux host regression: actual wget transport against an explicit local fixture."""
import io, json, pathlib, subprocess, threading, time, wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
root=pathlib.Path(__file__).resolve().parents[2]
audio=io.BytesIO()
with wave.open(audio,'wb') as wav:
    wav.setnchannels(1);wav.setsampwidth(2);wav.setframerate(16000);wav.writeframes(b'\0'*3200)
class Fixture(BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def do_GET(self):
        time.sleep(3)
        try:self.send_response(200);self.end_headers();self.wfile.write(b'late')
        except BrokenPipeError:pass
    def do_POST(self):
        body=self.rfile.read(int(self.headers['Content-Length']))
        if self.path=='/tts':
            j=json.loads(body);assert j['response_format']=='wav' and 'voice' not in j
            out=audio.getvalue()
        elif self.path=='/asr':
            assert b'name="model"\r\n\r\ntest\r\n' in body and audio.getvalue() in body
            out=json.dumps({'text':'你好，请打开计算器。'}).encode()
        elif self.path=='/bad-audio':out=b'not wav'
        else:
            j=json.loads(body);assert j['response_format']['type']=='json_object'
            if self.path=='/vision':
                assert j['messages'][-1]['content'][1]['image_url']['url'].startswith('data:image/jpeg;base64,')
                content=json.dumps({'action':'click','x':.5,'y':.5})
            elif self.path=='/chat':content=json.dumps({'reply':'已准备好，请按运行。','task':'打开计算器'})
            else:content='private internal analysis is not an action'
            out=json.dumps({'choices':[{'message':{'content':content}}]}).encode()
        self.send_response(200);self.send_header('Content-Length',str(len(out)));self.end_headers();self.wfile.write(out)
server=ThreadingHTTPServer(('127.0.0.1',0),Fixture)
thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
try:
    dest=root/'.build/moonpilot-tests';dest.mkdir(exist_ok=True)
    subprocess.run(['c++','-std=c++17','-g','-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all','-pthread','-Ishared','-I.build/include','moonpilot/tests/agent_test.cpp','moonpilot/src/agent.cpp','moonpilot/src/voice.cpp','shared/net.cpp','-o',str(dest/'core-test')],cwd=root,check=True)
    subprocess.run([str(dest/'core-test'),f'http://127.0.0.1:{server.server_port}'],cwd=root,check=True)
finally:
    server.shutdown();server.server_close();thread.join()
