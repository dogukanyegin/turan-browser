"""Inspect Chromium secure-DNS preferences and isolated fixture resolution NetLogs."""
import http.server
import json
from pathlib import Path
import secrets
import socket
import subprocess
import tempfile
import threading
import time
import urllib.request
from ui_smoke import windows,u
from process_tree import hold_children,wait_children
from thirdparty_extensions_cef import evaluate

ROOT=Path(__file__).resolve().parents[1]

def until(test,seconds=20):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        value=test()
        if value:return value
        time.sleep(.1)
    raise AssertionError('Timed out waiting for DNS test state')

def main():
    report={'scope':'Isolated public fixture hostname, Chromium NetLog; not a complete packet-level leak audit'}
    class Server(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers();self.wfile.write(b'<title>DNS fixture</title>')
        def log_message(self,*args):pass
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    try:
        for secure,blocked in ((False,False),(True,False),(True,True)):
          with tempfile.TemporaryDirectory(prefix='turan-dns-') as temporary:
            profile=Path(temporary);(profile/'browser-state.txt').write_text('S 1 1 0 '+str(int(secure))+' 1 0\n')
            with socket.socket() as free:free.bind(('127.0.0.1',0));port=free.getsockname()[1]
            url='http://127.0.0.1:'+str(server.server_port)+'/dns';exe=ROOT/'out-cef/app/Release/TuranBrowser.exe'
            netlog=profile/'network-test.json';diagnostic=profile/'network-configuration.txt'
            arguments=[str(exe),'--profile='+temporary,'--url='+url,'--turan-test-dns',
                '--remote-debugging-port='+str(port),'--log-net-log='+str(netlog)]
            if blocked:arguments.append('--host-resolver-rules=MAP cloudflare-dns.com ~NOTFOUND')
            proc=subprocess.Popen(arguments,cwd=exe.parent)
            handles=[];mode='secure' if secure else 'off';label='secure-provider-unavailable' if blocked else mode
            case={};report[label]=case
            try:
                window=until(lambda:windows(proc.pid,'TuranWindow'))[0]
                case['preferences']=diagnostic.read_text();assert 'APPLIED 1' in case['preferences'] and 'MODE '+mode in case['preferences']
                def targets():
                    try:return json.load(urllib.request.urlopen('http://127.0.0.1:'+str(port)+'/json/list',timeout=2))
                    except OSError:return []
                target=until(lambda:next((row for row in targets() if row['url']==url),None))
                until(lambda:evaluate(target,'location.href==='+json.dumps(url)+'&&document.readyState==="complete"'))
                hostname='turan-cef-'+secrets.token_hex(4)+'.example.org';case['hostname']=hostname
                evaluate(target,'window.dnsResult="pending";fetch("https://'+hostname+'/",{mode:"no-cors"}).then(()=>window.dnsResult="response").catch(e=>window.dnsResult=e.name);"started"')
                until(lambda:evaluate(target,'window.dnsResult')!='pending',30)
                case['rendererResult']=evaluate(target,'window.dnsResult')
                handles=hold_children(proc.pid);u.PostMessageW(window,0x10,0,0)
                assert proc.wait(timeout=15)==0;wait_children(handles);handles=[]
                log=json.loads(netlog.read_text());types={number:name for name,number in log['constants']['logEventTypes'].items()}
                events=log['events']
                transactions={event['source']['id'] for event in events if types[event['type']]=='DNS_TRANSACTION' and hostname in json.dumps(event.get('params',{}))}
                related=[event for event in events if event['source']['id'] in transactions]
                case['transactionEvents']=[{'type':types[event['type']],'phase':event['phase'],'params':event.get('params',{})} for event in related]
                case['hostResolverEvents']=[{'type':types[event['type']],'params':event.get('params',{})} for event in events if hostname in json.dumps(event.get('params',{}))]
                case['httpsAttempts']=sum(types[event['type']]=='DNS_TRANSACTION_HTTPS_ATTEMPT' for event in related)
                # DNS_TRANSACTION_QUERY is emitted for every transport. Count actual classic attempts.
                case['classicAttempts']=sum(types[event['type']] in ('DNS_TRANSACTION_ATTEMPT','DNS_TRANSACTION_TCP_ATTEMPT') for event in related)
                # A failed attempt also emits RESPONSE with only a dependency. Require a parsed DNS rcode.
                case['httpsResponses']=sum(types[event['type']]=='DNS_TRANSACTION_RESPONSE' and 'rcode' in event.get('params',{}) for event in related) if secure else 0
                (ROOT/'artifacts'/('dns-netlog-'+label+'.json')).write_text(json.dumps(log))
                assert case['hostResolverEvents'],'No fixture resolution in NetLog'
                if secure:
                    assert case['httpsAttempts']>0,'Configured DoH did not attempt fixture resolution over HTTPS'
                    assert case['classicAttempts']==0,'Secure fixture resolution made a classic DNS attempt'
                    if blocked:assert case['httpsResponses']==0,'Provider block did not prevent DoH responses'
                    else:assert case['httpsResponses']>0,'DoH did not receive an actual DNS response'
                else:assert case['classicAttempts']>0,'Negative control did not exercise classic DNS'
                case['passed']=True
            except Exception as error:case['failure']=str(error);raise
            finally:
                if proc.poll() is None:
                    handles=handles or hold_children(proc.pid)
                    subprocess.run(['taskkill','/PID',str(proc.pid),'/T','/F'],capture_output=True);proc.wait(timeout=15)
                if handles:wait_children(handles)
    finally:
        server.shutdown();server.server_close()
        (ROOT/'artifacts/dns-cef.json').write_text(json.dumps(report,indent=2)+'\n')
        summary={key:{field:value for field,value in case.items() if field in ('preferences','httpsAttempts','classicAttempts','httpsResponses','passed','failure')} for key,case in report.items() if isinstance(case,dict)}
        print(json.dumps(summary,indent=2))

if __name__=='__main__':main()
