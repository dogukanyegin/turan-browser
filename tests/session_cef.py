"""Real CEF restart/crash recovery and private-session exclusion tests."""
import argparse
import ctypes
import contextlib
import http.server
from pathlib import Path
import subprocess
import tempfile
import threading
import time
from ui_smoke import windows,u,navigate_bar
from process_tree import hold_children,wait_children
from transient_state_read import read_optional_bytes

ROOT=Path(__file__).resolve().parents[1]

def wait_for(test, timeout=15):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        value=test()
        if value: return value
        time.sleep(.1)
    raise AssertionError('Timed out waiting for engine/session state')

def main(exe):
    requests=[]
    class Server(http.server.BaseHTTPRequestHandler):
        def handle(self):
            try:super().handle()
            except ConnectionResetError:pass # expected when deliberately killing CEF
        def do_GET(self):
            requests.append(self.path)
            data=('<title>'+self.path+'</title><p>Session fixture</p>').encode()
            self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers();self.wfile.write(data)
        def log_message(self,*args):pass
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    base='http://127.0.0.1:'+str(server.server_port)
    exe=exe.resolve();process=None
    def launch(profile,url=None):
        nonlocal process
        arguments=[str(exe),'--profile='+str(profile)]+(['--url='+url] if url else [])
        process=subprocess.Popen(arguments,cwd=exe.parent)
        window=wait_for(lambda: windows(process.pid,'TuranWindow'))[0]
        return process,window
    def command(window,id):u.SendMessageW(window,0x111,id,0)
    def navigate(window,url):
        navigate_bar(window,url)
    def close(process,window):
        children=hold_children(process.pid)
        u.PostMessageW(window,0x10,0,0);assert process.wait(timeout=15)==0
        wait_children(children)
    @contextlib.contextmanager
    def process_cleanup():
        try:yield
        finally:
            if process and process.poll() is None:
                children=hold_children(process.pid)
                subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],capture_output=True)
                process.wait(timeout=10)
                wait_children(children)
    try:
        with tempfile.TemporaryDirectory(prefix='turan-session-cef-') as temp, process_cleanup():
            profile=Path(temp)
            legacy='S 1 1 0 0 1 0\nB "Legacy bookmark" "https://legacy.example"\nH "Legacy history" "https://history.example"\n'
            (profile/'browser-state.txt').write_text(legacy)
            journal=profile/'session-v1.txt'
            state=lambda:journal.read_text(encoding='utf-8') if journal.exists() else ''
            process,window=launch(profile,base+'/first')
            wait_for(lambda:'/first' in state())
            command(window,221);command(window,222);command(window,223)
            command(window,103);time.sleep(.5);navigate(window,base+'/second')
            wait_for(lambda:'/second' in state())
            command(window,104)
            wait_for(lambda:any(line.startswith('C ') and '/second' in line for line in state().splitlines()))
            command(window,220)
            wait_for(lambda:sum(line.startswith('T ') for line in state().splitlines())==2)
            command(window,103);time.sleep(.5);navigate(window,base+'/third')
            wait_for(lambda:'/third' in state())
            tab_control=u.GetDlgItem(window,10)
            wait_for(lambda:u.SendMessageW(tab_control,0x1304,0,0)==3)
            def center(index):
                # Fixed-width native tab items. TCM_GETITEMRECT pointers cannot
                # be marshalled across processes by SendMessage.
                return (85+170*index)|(20<<16)
            u.SendMessageW(tab_control,0x201,1,center(2))
            u.SendMessageW(tab_control,0x202,0,center(1))
            wait_for(lambda:state().find('/third')<state().find('/second'))
            def caption():
                length=u.SendMessageW(window,0xE,0,0);value=ctypes.create_unicode_buffer(length+1)
                u.SendMessageW(window,0xD,length+1,ctypes.cast(value,ctypes.c_void_p).value);return value.value
            # A private tab is created asynchronously. Do not navigate the still-
            # active normal tab and then mistake that fixture race for a leak.
            command(window,105)
            wait_for(lambda:'Private' in caption() and u.IsWindowEnabled(u.GetDlgItem(window,20)),20)
            navigate(window,base+'/private-secret')
            wait_for(lambda:'/private-secret' in requests)
            wait_for(lambda:'/private-secret' in caption() and 'Private' in caption())
            # Strong fail-closed privacy boundary: even in the correctly active
            # private tab the URL must never appear in either journal or backup.
            def no_private_journal_url():
                primary=state()
                backup=profile/'session-v1.bak'
                old=backup.read_text(encoding='utf-8',errors='replace') if backup.exists() else ''
                if '/private-secret' in primary or '/private-secret' in old:
                    raise AssertionError('PRIVATE SESSION URL WRITTEN TO DISK: block Beta publication; inspect normal/private tab routing')
                return True
            no_private_journal_url()
            close(process,window)
            assert state().startswith('TURAN_SESSION 1 1 ')
            metadata=(profile/'browser-state.txt').read_text(encoding='utf-8')
            assert 'Legacy bookmark' in metadata and 'Legacy history' in metadata
            assert '/private-secret' not in metadata
            assert '"Work" 1' in state() and ' 1 1 "Work"' in state()
            process,window=launch(profile)
            tabs=u.GetDlgItem(window,10)
            wait_for(lambda:u.SendMessageW(tabs,0x1304,0,0)==3)
            wait_for(lambda:state().startswith('TURAN_SESSION 1 0 '))
            children=hold_children(process.pid)
            subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],capture_output=True,check=True)
            process.wait(timeout=10)
            wait_children(children)
            assert state().startswith('TURAN_SESSION 1 0 ')
            process,window=launch(profile)
            wait_for(lambda:u.SendMessageW(u.GetDlgItem(window,10),0x1304,0,0)==3)
            close(process,window)
            # Legacy metadata recovery must work with the actual engine, not only a parser unit test.
            journal.write_text('truncated journal')
            process,window=launch(profile)
            wait_for(lambda:u.SendMessageW(u.GetDlgItem(window,10),0x1304,0,0)==3)
            close(process,window)
            primary=profile/'browser-state.txt';backup=profile/'browser-state.bak'
            assert backup.is_file() and b'Legacy bookmark' in backup.read_bytes()
            damaged=b'S 1 1 0 0 1 0\nB "truncated bookmark'
            primary.write_bytes(damaged)
            process,window=launch(profile,base+'/metadata-recovered')
            # A damaged primary is first moved to a .rejected-* quarantine.
            # Between quarantine and the atomic recovery write, the primary
            # path is legitimately absent. A transient FileNotFoundError is
            # NOT a failed recovery; wait for the valid restored bytes.
            # Other I/O errors still fail. A permanently absent or damaged
            # primary still times out rather than being accepted.
            def primary_recovered():
                payload=read_optional_bytes(primary)
                return payload is not None and (b'Legacy bookmark' in payload
                                                and b'Legacy history' in payload)
            wait_for(primary_recovered)
            close(process,window)
            assert b'Legacy history' in primary.read_bytes()
            rejected=list(profile.glob('browser-state.txt.rejected-*'))
            assert len(rejected)==1 and rejected[0].read_bytes()==damaged
            # With no valid copy, stop before CEF/profile writes and acknowledge only our own dialog.
            primary.write_bytes(damaged);backup.write_bytes(b'not valid metadata')
            journal_before=journal.read_bytes();backup_before=backup.read_bytes()
            process=subprocess.Popen([str(exe),'--profile='+str(profile),'--url='+base+'/must-not-load'],cwd=exe.parent)
            dialog=wait_for(lambda:windows(process.pid,'#32770'))[0]
            assert not windows(process.pid,'TuranWindow')
            # Windows can assign IDCANCEL even to the sole acknowledgement button.
            acknowledge=wait_for(lambda:u.GetDlgItem(dialog,1) or u.GetDlgItem(dialog,2))
            u.PostMessageW(acknowledge,0xf5,0,0) # BM_CLICK on this process's button only
            assert process.wait(timeout=15)==3
            assert primary.read_bytes()==damaged and backup.read_bytes()==backup_before
            assert journal.read_bytes()==journal_before
    finally:
        if process and process.poll() is None:subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],capture_output=True)
        server.shutdown();server.server_close()
    proof='PASS real CEF clean restart, killed-process recovery, corrupted journal backup, closed-tab reopen, native drag reorder, pin/mute/group persistence, private history/session exclusion, legacy bookmark/history preservation, metadata backup recovery and rejected-file preservation, invalid metadata startup refusal without overwrites\n'
    (ROOT/'artifacts/session-cef.txt').write_text(proof);print(proof)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,default=ROOT/'out-cef/app/Release/TuranBrowser.exe')
    main(parser.parse_args().exe)
