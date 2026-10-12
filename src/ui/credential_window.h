#pragma once
#include <windows.h>
#include <filesystem>
#include <functional>
#include "include/cef_browser.h"
namespace turan {
// Modeless native credential manager; profile vault never receives page messages.
// Parent destruction closes the manager. The callback resolves the active tab at
// the moment of an explicit action, including its private classification.
HWND OpenCredentialWindow(HWND owner,const std::filesystem::path& profile,bool turkish,
  std::function<CefRefPtr<CefBrowser>()> active,std::function<bool(CefRefPtr<CefBrowser>)> is_private);
}
