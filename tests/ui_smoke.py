"""Exercise the built native Win32 controls without capturing the desktop."""
import ctypes
import argparse
from ctypes import wintypes
from pathlib import Path
import subprocess
import tempfile
import time
from process_tree import hold_children,wait_children

ROOT=Path(__file__).resolve().parents[1]
u=ctypes.WinDLL('user32',use_last_error=True)
CALLBACK=ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM)
u.EnumWindows.argtypes=[CALLBACK,wintypes.LPARAM]
u.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
u.GetClassNameW.argtypes=[wintypes.HWND,wintypes.LPWSTR,ctypes.c_int]
u.SendMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM];u.SendMessageW.restype=wintypes.LPARAM
u.GetDlgItem.argtypes=[wintypes.HWND,ctypes.c_int];u.GetDlgItem.restype=wintypes.HWND
u.IsWindowEnabled.argtypes=[wintypes.HWND];u.IsWindowEnabled.restype=wintypes.BOOL
u.PostMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
u.GetWindowRect.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.RECT)]

def windows(pid,cls):
    matches=[]
    @CALLBACK
    def callback(hwnd,unused):
        owner=wintypes.DWORD();u.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
        name=ctypes.create_unicode_buffer(256);u.GetClassNameW(hwnd,name,256)
        if owner.value==pid and name.value==cls:matches.append(hwnd)
        return True
    u.EnumWindows(callback,0);return matches

def navigate_bar(window,url,timeout=15):
    """Use the native omnibox after async browser creation, with real edit focus."""
    edit=u.GetDlgItem(window,20);deadline=time.monotonic()+timeout
    while not u.IsWindowEnabled(edit):
        if time.monotonic()>deadline:raise AssertionError('Native omnibox did not become ready')
        time.sleep(.05)
    point=10|(12<<16);u.SendMessageW(edit,0x201,1,point);u.SendMessageW(edit,0x202,0,point)
    value=ctypes.create_unicode_buffer(url)
    u.SendMessageW(edit,0xC,0,ctypes.cast(value,ctypes.c_void_p).value)
    u.SendMessageW(edit,0x100,0xD,0)

def main(exe,portable=False):
    with tempfile.TemporaryDirectory(prefix='turan-ui-') as temp:
        proc=subprocess.Popen([str(exe)]+([] if portable else ['--profile='+temp]),cwd=exe.parent)
        children=[]
        try:
            deadline=time.time()+20;main_window=None
            while time.time()<deadline:
                found=windows(proc.pid,'TuranWindow')
                if found:main_window=found[0];break
                if proc.poll() is not None:raise RuntimeError('Browser exited before native window creation')
                time.sleep(.1)
            assert main_window
            time.sleep(2)
            tab=u.GetDlgItem(main_window,10)
            assert u.SendMessageW(tab,0x1304,0,0)==1 # TCM_GETITEMCOUNT
            for command in (200,201,202,203,204):
                u.SendMessageW(main_window,0x111,command,0)
                panel=windows(proc.pid,'TuranPanel')[0]
                assert u.GetDlgItem(panel,40 if command==204 else 30)
                u.SendMessageW(panel,0x10,0,0)
            u.SendMessageW(main_window,0x111,103,0) # New tab
            time.sleep(.8)
            assert u.SendMessageW(tab,0x1304,0,0)==2
            u.SendMessageW(main_window,0x111,105,0) # Private tab
            time.sleep(.8)
            assert u.SendMessageW(tab,0x1304,0,0)==3
            u.SendMessageW(main_window,0x111,104,0) # Close active private tab
            time.sleep(.8)
            assert u.SendMessageW(tab,0x1304,0,0)==2
            children=hold_children(proc.pid)
            u.PostMessageW(main_window,0x10,0,0)
            assert proc.wait(timeout=15)==0
            wait_children(children);children=[]
            if portable:assert (exe.parent/'Profile/browser-state.txt').exists()
            print('PASS native panels, tab create/close, private tab, clean window shutdown')
            (ROOT/'artifacts/native-ui-test.txt').write_text('PASS native panels, tab create/close, private tab, clean shutdown\n')
        finally:
            if proc.poll() is None:
                children=children or hold_children(proc.pid)
                subprocess.run(['taskkill','/PID',str(proc.pid),'/T','/F'],capture_output=True)
                proc.wait(timeout=15)
            if children:wait_children(children)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,default=ROOT/'out-cef/app/Release/TuranBrowser.exe')
    parser.add_argument('--portable',action='store_true')
    args=parser.parse_args();main(args.exe.resolve(),args.portable)
