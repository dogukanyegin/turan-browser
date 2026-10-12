#pragma once
#include <windows.h>
#include <string>

namespace turan {
// Opt-in architecture probe: retain Chromium's genuine TYPE_NORMAL tab model.
// Engine ownership is unchanged; this adapter never destroys a CEF window.
// Stock Chromium controls remain visible until a supported custom-UI solution
// has been established. This is deliberately not the default release runtime.
class NativeChromeHost {
 public:
  static bool Attach(HWND engine, HWND owner, std::wstring& error);
  static bool Layout(HWND engine, HWND owner, const RECT& bounds);
  static void ActivateWithinOwner(HWND engine,HWND owner);
};
}
