#include "ui/native_chrome_host.h"
#include <cwchar>

namespace turan {
namespace {
bool OwnedWindow(HWND window) {
  DWORD process=0;const DWORD thread=GetWindowThreadProcessId(window,&process);
  return IsWindow(window)&&process==GetCurrentProcessId()&&thread==GetCurrentThreadId();
}
}
bool NativeChromeHost::Attach(HWND engine,HWND owner,std::wstring& error) {
  if(!OwnedWindow(engine)||!OwnedWindow(owner)||engine==owner) {
    error=L"The Chromium frame and Turan owner must belong to the current UI thread.";return false;
  }
  wchar_t type[64]={};GetClassNameW(engine,type,64);
  if(std::wcscmp(type,L"Chrome_WidgetWin_1")!=0) {
    error=L"The engine did not supply the expected Chromium frame.";return false;
  }
  if(GetParent(engine)==owner)return true; // Multiple engine tabs share this HWND.
  if(GetAncestor(engine,GA_ROOT)!=engine||GetParent(engine)) {
    error=L"An already-owned engine window cannot be attached to another host.";return false;
  }
  if(!AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(engine),GetWindowDpiAwarenessContext(owner))) {
    error=L"Chromium and Turan have different DPI awareness; attachment was refused.";return false;
  }
  const LONG_PTR style=GetWindowLongPtrW(engine,GWL_STYLE);
  SetLastError(ERROR_SUCCESS);
  if(!SetWindowLongPtrW(engine,GWL_STYLE,(style&~(WS_POPUP|WS_OVERLAPPEDWINDOW))|WS_CHILD)&&GetLastError()) {
    error=L"The Chromium frame style could not be changed.";return false;
  }
  // A null return can also mean successful attachment of a former root window.
  SetLastError(ERROR_SUCCESS);SetParent(engine,owner);
  if(GetParent(engine)!=owner) {
    SetWindowLongPtrW(engine,GWL_STYLE,style);
    SetWindowPos(engine,nullptr,0,0,0,0,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED);
    error=L"Chromium could not be attached to the Turan native window.";return false;
  }
  SetWindowPos(engine,nullptr,0,0,0,0,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED);
  return true;
}
bool NativeChromeHost::Layout(HWND engine,HWND owner,const RECT& bounds) {
  if(!OwnedWindow(engine)||GetParent(engine)!=owner)return false;
  return SetWindowPos(engine,nullptr,bounds.left,bounds.top,
    bounds.right>bounds.left?bounds.right-bounds.left:1,
    bounds.bottom>bounds.top?bounds.bottom-bounds.top:1,
    SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE;
}
void NativeChromeHost::ActivateWithinOwner(HWND engine,HWND owner) {
  if(!OwnedWindow(engine)||GetParent(engine)!=owner)return;
  // Chromium's native widget tests GetActiveWindow() against its frame. Never
  // activate an application that the user has left in the background.
  HWND foreground=GetForegroundWindow();
  if(foreground==owner||GetAncestor(foreground,GA_ROOT)==owner)SetActiveWindow(engine);
}
}
