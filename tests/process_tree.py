"""Win32 test helpers: observe only the launched browser's process descendants."""
import ctypes
from ctypes import wintypes as w
import time

k=ctypes.WinDLL('kernel32',use_last_error=True)
class ProcessEntry(ctypes.Structure):
    _fields_=[('dwSize',w.DWORD),('cntUsage',w.DWORD),('pid',w.DWORD),('heap',ctypes.c_size_t),
              ('module',w.DWORD),('threads',w.DWORD),('parent',w.DWORD),('priority',w.LONG),
              ('flags',w.DWORD),('exe',w.WCHAR*260)]
k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];k.CreateToolhelp32Snapshot.restype=w.HANDLE
k.Process32FirstW.argtypes=[w.HANDLE,ctypes.POINTER(ProcessEntry)];k.Process32FirstW.restype=w.BOOL
k.Process32NextW.argtypes=k.Process32FirstW.argtypes;k.Process32NextW.restype=w.BOOL
k.CloseHandle.argtypes=[w.HANDLE]
k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
k.WaitForSingleObject.argtypes=[w.HANDLE,w.DWORD];k.WaitForSingleObject.restype=w.DWORD
k.GetProcessTimes.argtypes=[w.HANDLE]+[ctypes.POINTER(w.FILETIME)]*4;k.GetProcessTimes.restype=w.BOOL

def creation_time(pid):
    handle=k.OpenProcess(0x1000,False,pid)
    if not handle:return 0
    try:
        times=[w.FILETIME() for _ in range(4)]
        if not k.GetProcessTimes(handle,*[ctypes.byref(value) for value in times]):return 0
        return (times[0].dwHighDateTime<<32)+times[0].dwLowDateTime
    finally:k.CloseHandle(handle)

def descendants(root):
    snapshot=k.CreateToolhelp32Snapshot(2,0)
    if snapshot==ctypes.c_void_p(-1).value:raise ctypes.WinError(ctypes.get_last_error())
    entries={};entry=ProcessEntry();entry.dwSize=ctypes.sizeof(entry)
    try:
        more=k.Process32FirstW(snapshot,ctypes.byref(entry))
        while more:
            entries[entry.pid]=(entry.parent,entry.exe)
            more=k.Process32NextW(snapshot,ctypes.byref(entry))
    finally:k.CloseHandle(snapshot)
    if root not in entries:return {root}
    root_name=entries[root][1].lower();root_created=creation_time(root)
    # Windows retains historical parent IDs and reuses IDs. Do not capture an
    # unrelated compiler/browser whose former parent happens to reuse our PID.
    entries={pid:row for pid,row in entries.items() if row[1].lower()==root_name and creation_time(pid)>=root_created}
    found={root}
    while True:
        extra={pid for pid,(parent,_) in entries.items() if parent in found}
        if extra.issubset(found):return found
        found.update(extra)

def hold_children(root):
    return [(pid,k.OpenProcess(0x100000,False,pid)) for pid in descendants(root) if pid!=root]

def wait_children(handles,timeout=10):
    deadline=time.monotonic()+timeout;alive=[]
    try:
        for pid,handle in handles:
            if handle and k.WaitForSingleObject(handle,max(0,int((deadline-time.monotonic())*1000)))!=0:alive.append(pid)
    finally:
        for _,handle in handles:
            if handle:k.CloseHandle(handle)
    if alive:raise AssertionError('Browser child processes did not exit: '+str(alive))
