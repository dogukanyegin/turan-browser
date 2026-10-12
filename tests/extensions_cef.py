"""Sandboxed CEF Chrome-style MV3 probe; isolated profile and local server only."""
import argparse
import http.server
import json
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import ctypes
from ui_smoke import windows, u, navigate_bar

ROOT = Path(__file__).resolve().parents[1]

def main(exe, topology_test=False,runtime='chrome'):
    report = {}; received = threading.Event(); requests = []
    class Server(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            requests.append(self.path)
            data = b'<title>MV3 fixture</title><p id="fixture">Turan fixture</p>'
            self.send_response(200); self.send_header('Content-Type', 'text/html')
            self.end_headers(); self.wfile.write(data)
        def do_POST(self):
            if self.path == '/report':
                report.update(json.loads(self.rfile.read(int(self.headers['Content-Length'])))); received.set()
            self.send_response(204); self.end_headers()
        def log_message(self, *args): pass
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Server)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    proof = {'engine': json.loads((ROOT/'cef.lock.json').read_text())['version'], 'runtime': runtime, 'sandboxDisabled': False}
    try:
        with tempfile.TemporaryDirectory(prefix='turan-mv3-') as temp:
            exe = exe.resolve()
            process = subprocess.Popen([str(exe), '--profile='+temp, '--turan-runtime='+runtime,
                '--load-extension='+str(ROOT/'tests/extensions/mv3_probe'),
                '--url=http://127.0.0.1:'+str(server.server_port)+'/fixture'], cwd=exe.parent)
            try:
                deadline = time.monotonic()+30
                while time.monotonic()<deadline and not received.wait(.2):
                    if process.poll() is not None: break
                if topology_test and report:
                    first_report = dict(report); report.clear(); received.clear()
                    window = windows(process.pid, 'TuranWindow')[0]
                    u.SendMessageW(window, 0x111, 103, 0)
                    time.sleep(1)
                    navigate_bar(window,'http://127.0.0.1:'+str(server.server_port)+'/fixture?second')
                    received.wait(15)
                    proof['firstReport'] = first_report
                    proof['nativeTabs'] = u.SendMessageW(u.GetDlgItem(window,10),0x1304,0,0)
                    fixture_windows = [row for row in report.get('topology', []) if any('/fixture' in tab.get('url','') for tab in row['tabs'])]
                    proof['oneNativeWindowHasOneExtensionWindow'] = len(fixture_windows) == 1
                proof.update(report=report, blockedRequestReachedServer='/dnr-block' in requests, exitBeforeClose=process.poll())
                for hwnd in windows(process.pid, 'TuranWindow'): u.PostMessageW(hwnd, 0x10, 0, 0)
                try: proof['exitCode'] = process.wait(timeout=15)
                except subprocess.TimeoutExpired: proof['shutdownTimedOut'] = True
                log = Path(temp)/'cef.log'
                if log.exists(): proof['engineLog'] = log.read_text(errors='replace')[-12000:]
            finally:
                if process.poll() is None: subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],capture_output=True)
    finally: server.shutdown(); server.server_close()
    (ROOT/'artifacts').mkdir(exist_ok=True)
    filename='mv3-topology-'+runtime+'.json' if topology_test else 'mv3-probe.json'
    (ROOT/'artifacts'/filename).write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps(proof,indent=2),flush=True)
    results = report.get('results', {})
    assert results and all(value is True for value in results.values()), 'MV3 acceptance probe failed; see artifacts/mv3-probe.json'
    assert '/dnr-block' not in requests, 'DNR did not block the actual network request'
    assert proof.get('exitCode') == 0, 'Chrome-style shutdown failed'
    if topology_test:
        assert proof['nativeTabs']==2, 'Native shell did not register both tabs'
        assert proof['oneNativeWindowHasOneExtensionWindow'], 'Native tab and extension window topology differ'

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,default=ROOT/'out-cef/app/Release/TuranBrowser.exe')
    parser.add_argument('--topology',action='store_true')
    parser.add_argument('--runtime',choices=['chrome','chrome-tabs','chrome-native-tabs','chrome-reparent-tabs'],default='chrome')
    args=parser.parse_args();main(args.exe,args.topology,args.runtime)
