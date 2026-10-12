#include "ui/engine_window.h"
#include <commctrl.h>
#include <cwchar>
#include <iterator>
#include <algorithm>

namespace turan {
namespace {
// WM_SYSCOMMAND masks the low four bits. Keep 0x1F00 reserved for Turan.
constexpr UINT kMenu=0x1F00;
constexpr UINT kShield=0x1F10,kExtensions=0x1F20,kDownloads=0x1F30;
constexpr UINT kHistory=0x1F40,kSettings=0x1F50,kUpdates=0x1F60;
constexpr UINT kPermissions=0x1F70,kBookmarks=0x1F80,kSavePdf=0x1F90,kPrint=0x1FA0;
constexpr UINT_PTR kSubclass=0x54555241;
constexpr int kCollapsed=40,kExpanded=172,kButtonHeight=30;
struct NativeAction {UINT system;int command;const wchar_t* label;};
constexpr NativeAction kActions[]={
  {kShield,107,L"Turan Shield..."},{kExtensions,214,L"Extensions..."},
  {kDownloads,202,L"Downloads..."},{kHistory,201,L"History..."},
  {kBookmarks,200,L"Bookmarks..."},{kPermissions,203,L"Site permissions..."},
  {kSettings,204,L"Turan settings..."},{kUpdates,216,L"Secure updates..."},
  {kSavePdf,247,L"Save PDF..."},{kPrint,246,L"Print..."},
};
constexpr NativeAction kDockActions[]={
  {0,107,L"Shield"},{0,214,L"Extensions"},{0,200,L"Bookmarks"},
  {0,201,L"History"},{0,202,L"Downloads"},{0,203,L"Permissions"},
  {0,204,L"Turan settings"},{0,216,L"Secure updates"},
  {0,247,L"Save PDF"},{0,246,L"Print"},{0,108,L"More commands..."},
};
constexpr int kToggle=500;
constexpr wchar_t kDockClass[]=L"TuranEngineTools";
int Scale(HWND frame,int value) {
  const UINT dpi=GetDpiForWindow(frame);
  return MulDiv(value,dpi?static_cast<int>(dpi):96,96);
}
bool Owned(HWND frame) {
  DWORD process=0;auto thread=GetWindowThreadProcessId(frame,&process);
  wchar_t name[64]={};GetClassNameW(frame,name,64);
  return IsWindow(frame)&&process==GetCurrentProcessId()&&thread==GetCurrentThreadId()&&
    GetAncestor(frame,GA_ROOT)==frame&&!GetParent(frame)&&std::wcscmp(name,L"Chrome_WidgetWin_1")==0;
}
}
EngineWindows::EngineWindows(std::function<void(HWND,int)> command):command_(std::move(command)){}
EngineWindows::~EngineWindows() {
  while(!frames_.empty())Release(frames_.begin()->first);
}
void EngineWindows::Release(HWND frame) {
  auto iter=frames_.find(frame);if(iter==frames_.end())return;
  const HWND dock=iter->second.dock;
  frames_.erase(iter);
  if(IsWindow(frame)) {
    RemoveWindowSubclass(frame,Proc,kSubclass);
    if(auto menu=GetSystemMenu(frame,FALSE)) {
      DeleteMenu(menu,kMenu,MF_BYCOMMAND);
      for(const auto& action:kActions)DeleteMenu(menu,action.system,MF_BYCOMMAND);
    }
  }
  if(IsWindow(dock))DestroyWindow(dock);
}
bool EngineWindows::CreateDock(HWND frame,std::wstring& error) {
  static bool registered=false;
  if(!registered) {
    WNDCLASSW cls={};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=DockProc;
    cls.lpszClassName=kDockClass;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);
    if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) {
      error=L"The native Turan command dock could not be registered.";return false;
    }
    registered=true;
  }
  // A separate *owned* tool widget is allowed. It is NOT a SetParent child and
  // does not change the actual Chromium root, extension focus or tab identity.
  HWND dock=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,kDockClass,L"Turan tools",
    WS_POPUP|WS_CLIPCHILDREN,0,0,kCollapsed,kCollapsed,frame,nullptr,GetModuleHandleW(nullptr),this);
  if(!dock) {error=L"Cannot create the Turan command dock.";return false;}
  frames_.at(frame).dock=dock;
  if(!CreateWindowExW(0,L"BUTTON",L"T",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
    Scale(frame,4),Scale(frame,4),Scale(frame,kCollapsed-8),Scale(frame,32),
    dock,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kToggle)),GetModuleHandleW(nullptr),nullptr)) {
    DestroyWindow(dock);frames_.at(frame).dock=nullptr;
    error=L"Cannot create the Turan toggle control.";return false;
  }
  for(size_t i=0;i<std::size(kDockActions);++i) {
    const auto& a=kDockActions[i];
    if(!CreateWindowExW(0,L"BUTTON",a.label,WS_CHILD|BS_PUSHBUTTON|WS_TABSTOP,
        Scale(frame,4),Scale(frame,40+static_cast<int>(i)*kButtonHeight),
        Scale(frame,kExpanded-8),Scale(frame,kButtonHeight-2),
        dock,reinterpret_cast<HMENU>(static_cast<INT_PTR>(a.command)),GetModuleHandleW(nullptr),nullptr)) {
      DestroyWindow(dock);frames_.at(frame).dock=nullptr;
      error=L"Cannot initialize all Turan native controls.";return false;
    }
  }
  PositionDock(frame);return true;
}
void EngineWindows::PositionDock(HWND frame) {
  auto iter=frames_.find(frame);if(iter==frames_.end()||!IsWindow(iter->second.dock))return;
  const auto& entry=iter->second;
  if(IsIconic(frame)||!IsWindowVisible(frame)) {ShowWindow(entry.dock,SW_HIDE);return;}
  RECT rect={};if(!GetWindowRect(frame,&rect))return;
  const int fullHeight=44+static_cast<int>(std::size(kDockActions))*kButtonHeight;
  // Chrome's own tab/extension controls must stay unobstructed. On a small
  // screen a collapsed handle is retained instead of overflowing its owner.
  if(rect.right-rect.left<Scale(frame,320)||rect.bottom-rect.top<Scale(frame,240)){
    ShowWindow(entry.dock,SW_HIDE);return;
  }
  const bool expanded=entry.expanded && rect.bottom-rect.top>=Scale(frame,155+fullHeight+12);
  const int width=Scale(frame,expanded?kExpanded:kCollapsed);
  const int height=Scale(frame,expanded?fullHeight:kCollapsed);
  for(const auto& action:kDockActions)if(auto control=GetDlgItem(entry.dock,action.command))
    ShowWindow(control,expanded?SW_SHOW:SW_HIDE);
  if(auto toggle=GetDlgItem(entry.dock,kToggle))
    MoveWindow(toggle,Scale(frame,4),Scale(frame,4),Scale(frame,kCollapsed-8),Scale(frame,32),TRUE);
  for(size_t i=0;i<std::size(kDockActions);++i)if(auto button=GetDlgItem(entry.dock,kDockActions[i].command))
    MoveWindow(button,Scale(frame,4),Scale(frame,40+static_cast<int>(i)*kButtonHeight),
               Scale(frame,kExpanded-8),Scale(frame,kButtonHeight-2),TRUE);
  const int x=rect.right-width-Scale(frame,12),y=rect.top+Scale(frame,155);
  SetWindowPos(entry.dock,HWND_TOP,x,y,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
}
void EngineWindows::ToggleDock(HWND frame) {
  auto it=frames_.find(frame);if(it==frames_.end()||!IsWindow(it->second.dock))return;
  it->second.expanded=!it->second.expanded;
  PositionDock(frame);
}
bool EngineWindows::Bind(HWND frame,HICON icon,std::wstring& error) {
  if(frames_.count(frame))return true;
  if(!Owned(frame)){error=L"Chromium must own an unmodified top-level frame on the current UI thread.";return false;}
  if(!SetWindowSubclass(frame,Proc,kSubclass,reinterpret_cast<DWORD_PTR>(this))) {
    error=L"Turan could not register native frame commands.";return false;
  }
  auto menu=GetSystemMenu(frame,FALSE);
  if(!menu||!AppendMenuW(menu,MF_SEPARATOR,0,nullptr)||
     !AppendMenuW(menu,MF_STRING,kMenu,L"Toggle Turan tools\tCtrl+Shift+M")) {
    RemoveWindowSubclass(frame,Proc,kSubclass);
    error=L"Turan could not register its native menu.";return false;
  }
  for(const auto& a:kActions)if(!AppendMenuW(menu,MF_STRING,a.system,a.label)) {
    for(const auto& previous:kActions)DeleteMenu(menu,previous.system,MF_BYCOMMAND);
    DeleteMenu(menu,kMenu,MF_BYCOMMAND);RemoveWindowSubclass(frame,Proc,kSubclass);
    error=L"Turan could not register native browser commands.";return false;
  }
  frames_.emplace(frame,Frame{icon,nullptr,false});
  if(!CreateDock(frame,error)){Release(frame);return false;}
  SendMessageW(frame,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(icon));
  SendMessageW(frame,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(icon));
  return true;
}
bool EngineWindows::Contains(HWND frame) const {return frames_.count(frame)!=0;}
void EngineWindows::Brand(HWND frame,const std::wstring& title) {
  if(!Contains(frame)||!IsWindow(frame))return;
  SetWindowTextW(frame,(title+L" — Turan Browser").c_str());
}
LRESULT CALLBACK EngineWindows::DockProc(HWND dock,UINT msg,WPARAM w,LPARAM l) {
  if(msg==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
  if(msg==WM_COMMAND&&HIWORD(w)==BN_CLICKED) {
    auto frame=GetWindow(dock,GW_OWNER);
    auto self=reinterpret_cast<EngineWindows*>(GetWindowLongPtrW(dock,GWLP_USERDATA));
    if(self&&self->Contains(frame)) {
      int command=LOWORD(w);
      if(command==kToggle)self->ToggleDock(frame);
      else self->command_(frame,command);
    }
    return 0;
  }
  if(msg==WM_NCCREATE){auto create=reinterpret_cast<CREATESTRUCTW*>(l);
    SetWindowLongPtrW(dock,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(create->lpCreateParams));}
  if(msg==WM_NCDESTROY)SetWindowLongPtrW(dock,GWLP_USERDATA,0);
  return DefWindowProcW(dock,msg,w,l);
}
LRESULT CALLBACK EngineWindows::Proc(HWND frame,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR pointer) {
  auto self=reinterpret_cast<EngineWindows*>(pointer);
  if(msg==WM_SYSCOMMAND) {
    const auto selected=static_cast<UINT>(w&0xFFF0);
    if(selected==kMenu){self->ToggleDock(frame);return 0;}
    for(const auto& action:kActions)if(selected==action.system){self->command_(frame,action.command);return 0;}
  }
  if(msg==WM_KEYDOWN&&w=='M'&&(GetKeyState(VK_CONTROL)&0x8000)&&(GetKeyState(VK_SHIFT)&0x8000)) {
    self->ToggleDock(frame);return 0;
  }
  if(msg==WM_WINDOWPOSCHANGED||msg==WM_SIZE||msg==WM_SHOWWINDOW||msg==WM_DPICHANGED)
    self->PositionDock(frame);
  if(msg==WM_NCDESTROY)self->Release(frame);
  return DefSubclassProc(frame,msg,w,l);
}
}
