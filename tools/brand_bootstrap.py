"""Apply Turan icon and the compiled client DLL version to the local bootstrap."""
import ctypes
from ctypes import wintypes
from pathlib import Path
import struct
import sys

def brand(exe,icon,client=None):
    api=ctypes.WinDLL('kernel32',use_last_error=True)
    api.BeginUpdateResourceW.argtypes=[wintypes.LPCWSTR,wintypes.BOOL];api.BeginUpdateResourceW.restype=wintypes.HANDLE
    api.UpdateResourceW.argtypes=[wintypes.HANDLE,ctypes.c_void_p,ctypes.c_void_p,wintypes.WORD,ctypes.c_void_p,wintypes.DWORD];api.UpdateResourceW.restype=wintypes.BOOL
    api.EndUpdateResourceW.argtypes=[wintypes.HANDLE,wintypes.BOOL];api.EndUpdateResourceW.restype=wintypes.BOOL
    version=None
    if client:
        api.LoadLibraryExW.argtypes=[wintypes.LPCWSTR,wintypes.HANDLE,wintypes.DWORD];api.LoadLibraryExW.restype=wintypes.HANDLE
        api.FindResourceW.argtypes=[wintypes.HANDLE,ctypes.c_void_p,ctypes.c_void_p];api.FindResourceW.restype=wintypes.HANDLE
        api.SizeofResource.argtypes=[wintypes.HANDLE,wintypes.HANDLE];api.SizeofResource.restype=wintypes.DWORD
        api.LoadResource.argtypes=[wintypes.HANDLE,wintypes.HANDLE];api.LoadResource.restype=wintypes.HANDLE
        api.LockResource.argtypes=[wintypes.HANDLE];api.LockResource.restype=ctypes.c_void_p
        api.FreeLibrary.argtypes=[wintypes.HANDLE]
        module=api.LoadLibraryExW(str(client),None,2) # read resources, never execute DLL
        if not module:raise ctypes.WinError(ctypes.get_last_error())
        try:
            resource=api.FindResourceW(module,1,16)
            if not resource:raise ctypes.WinError(ctypes.get_last_error())
            size=api.SizeofResource(module,resource)
            pointer=api.LockResource(api.LoadResource(module,resource))
            if not size or not pointer:raise RuntimeError('Missing compiled version resource')
            version=ctypes.string_at(pointer,size)
        finally:api.FreeLibrary(module)
    data=Path(icon).read_bytes();count=struct.unpack_from('<H',data,4)[0]
    group=bytearray(struct.pack('<HHH',0,1,count));items=[]
    for index in range(count):
        w,h,colors,reserved,planes,bits,size,offset=struct.unpack_from('<BBBBHHII',data,6+index*16)
        group.extend(struct.pack('<BBBBHHIH',w,h,colors,reserved,planes,bits,size,index+1));items.append(data[offset:offset+size])
    handle=api.BeginUpdateResourceW(str(exe),False)
    if not handle:raise ctypes.WinError(ctypes.get_last_error())
    try:
        for index,raw in enumerate(items):
            buf=ctypes.create_string_buffer(raw)
            if not api.UpdateResourceW(handle,3,index+1,1033,buf,len(raw)):raise ctypes.WinError(ctypes.get_last_error())
        buf=ctypes.create_string_buffer(bytes(group))
        if not api.UpdateResourceW(handle,14,1,1033,buf,len(group)):raise ctypes.WinError(ctypes.get_last_error())
        if version:
            buf=ctypes.create_string_buffer(version)
            if not api.UpdateResourceW(handle,16,1,1033,buf,len(version)):raise ctypes.WinError(ctypes.get_last_error())
    except BaseException:
        api.EndUpdateResourceW(handle,True);raise
    if not api.EndUpdateResourceW(handle,False):raise ctypes.WinError(ctypes.get_last_error())

if __name__=='__main__':brand(Path(sys.argv[1]).resolve(),Path(sys.argv[2]).resolve(),Path(sys.argv[3]).resolve() if len(sys.argv)>3 else None)
