#pragma once
#include <windows.h>
#include <functional>
#include <map>
#include <string>

namespace turan {
// Chrome-style CEF retains ownership of its normal top-level window, tabs,
// focus and extension popups. Turan contributes an owned native command dock:
// NEVER change the engine HWND style, parent, or Chromium's tab-strip model.
class EngineWindows {
 public:
  explicit EngineWindows(std::function<void(HWND,int)> command);
  ~EngineWindows();
  bool Bind(HWND frame,HICON icon,std::wstring& error);
  void Brand(HWND frame,const std::wstring& title);
  bool Contains(HWND frame) const;
 private:
  struct Frame {HICON icon=nullptr;HWND dock=nullptr;bool expanded=false;};
  static LRESULT CALLBACK Proc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
  static LRESULT CALLBACK DockProc(HWND,UINT,WPARAM,LPARAM);
  bool CreateDock(HWND frame,std::wstring& error);
  void PositionDock(HWND frame);
  void ToggleDock(HWND frame);
  void Release(HWND frame);
  std::function<void(HWND,int)> command_;
  std::map<HWND,Frame> frames_;
};
}
