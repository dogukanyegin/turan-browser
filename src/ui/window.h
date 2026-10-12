#pragma once
#include <windows.h>
#include <map>
#include <memory>
#include <functional>
#include <future>
#include "privacy/filter_subscription.h"
#include "core/model.h"
#include "core/session.h"
#include "ui/engine_window.h"
#include "include/cef_browser.h"
#include "include/cef_download_handler.h"
#include "include/cef_request_context.h"

namespace turan {
struct Tab {
  int id; bool private_tab; CefRefPtr<CefBrowser> browser;
  std::string title="New tab",url; bool loading=false;
  bool pinned=false,muted=false;
  std::string group;unsigned color=0;
  bool engine_owned=false;
};
struct Download {
  std::string name,path; int percent=0; bool complete=false,canceled=false,paused=false;
  CefRefPtr<CefDownloadItemCallback> callback;
  bool recorded=false;
};
class Window {
 public:
  Window(Store& store, std::filesystem::path app_dir, std::string initial_url, std::filesystem::path smoke_result,bool chrome_probe=false,bool chrome_tabs=false,bool native_chrome=false,bool test_file_dialog=false,bool reparent_chrome=false,bool integrated_chrome=false);
  int BrowserTab(CefRefPtr<CefBrowser> browser) const;
  bool IsPrivateBrowser(CefRefPtr<CefBrowser> browser) const;
  CefRefPtr<CefClient> EngineClient();
  void EngineActivated(CefRefPtr<CefBrowser> browser);
  bool Create(HINSTANCE instance);
  void NewTab(bool private_tab=false,const std::string& url="");
  void Created(int id,CefRefPtr<CefBrowser> browser);
  void Closed(int id);
  void Address(int id,const std::string& url);
  void Title(int id,const std::string& title);
  void Loaded(int id);
  void Loading(int id,bool loading);
  void Error(int id,const std::string& text);
  void FindResult(int id,int count,int ordinal);
  void PageFullscreen(int id,bool fullscreen);
  void DownloadUpdated(CefRefPtr<CefBrowser> browser,CefRefPtr<CefDownloadItem> item,CefRefPtr<CefDownloadItemCallback> callback);
  void Permission(CefRefPtr<CefBrowser> browser,const std::string& origin,uint32_t kinds,const std::wstring& what,std::function<void(bool)> reply,uint64_t prompt_id=0,CefRefPtr<CefFrame> frame=nullptr);
  void DismissPermission(int browser_id,uint64_t prompt_id);
  void CancelPermissions(int browser_id);
  void CancelFramePermissions(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame> frame);
  bool Shortcut(int key,bool ctrl,bool shift);
  HWND handle() const {return hwnd_;}
  HWND UiOwner() const;
  bool IntegratedChrome() const {return integrated_chrome_;}
  const std::string& StartUrl() const {return start_url_;}
  std::shared_ptr<Shield> shield;
  bool is_smoke() const {return !smoke_result_.empty();}
  bool IsInternal(const std::string& url) const {
    return url==start_url_ || (chrome_probe_ && (url.rfind("chrome-extension://",0)==0 || url=="chrome://extensions/"));
  }
  std::filesystem::path SmokeDownload(bool incognito) const {return store_.root/(incognito?L"private-smoke-download.txt":L"smoke-download.txt");}
  std::filesystem::path TestPdfOutput() const {return test_file_dialog_?store_.root/L"navigation-test.pdf":std::filesystem::path();}
  void Close();
  bool BackupRequested() const {return backup_requested_;}
 private:
  static LRESULT CALLBACK Proc(HWND,UINT,WPARAM,LPARAM);
  static LRESULT CALLBACK EditProc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
  static LRESULT CALLBACK TabProc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
  static LRESULT CALLBACK PanelProc(HWND,UINT,WPARAM,LPARAM);
  static LRESULT CALLBACK PermissionProc(HWND,UINT,WPARAM,LPARAM);
  LRESULT Message(UINT,WPARAM,LPARAM);
  Tab* Find(int id);
  Tab* Active();
  void Layout();
  void Refresh();
  void Command(int id);
  void Select(int index);
  void NavigateBar();
  void Panel(int kind);
  void RefreshPanel(HWND panel,int kind);
  void SettingsApplied(HWND panel);
  void Save();
  void SaveSession(bool clean=false);
  void RebuildTabStrip();
  void MoveTab(int from,int to);
  void ReopenTab();
  void Fullscreen(bool enabled);
  void PollSubscriptions();
  std::wstring Text(const wchar_t* en,const wchar_t* tr) const;
  Store& store_;
  std::filesystem::path app_dir_,smoke_result_;
  std::string initial_url_,start_url_;
  HWND hwnd_=nullptr,tabs_control_=nullptr,omnibox_=nullptr,status_=nullptr;
  std::vector<HWND> buttons_;
  HFONT font_=nullptr;HBRUSH background_=nullptr;
  std::vector<Tab> tabs_;
  std::vector<HWND> panels_;
  std::vector<HWND> credential_windows_;
  std::vector<HWND> subscription_windows_;
  std::vector<HWND> update_windows_;
  std::shared_ptr<FilterSubscriptions> subscriptions_;
  std::future<std::shared_ptr<FilterSubscriptions>> subscription_initialization_;
  std::future<std::pair<uint64_t,std::string>> subscription_publication_;
  uint64_t subscription_seen_revision_=0;
  bool subscription_force_=false;
  std::string subscription_status_="Initializing subscription policy";
  std::vector<std::wstring> permissions_;
  std::vector<PermissionRule> private_permissions_;
  std::vector<HWND> permission_prompts_;
  std::map<uint64_t,Download> downloads_;
  CefRefPtr<CefRequestContext> normal_,private_;
  std::vector<SessionTab> closed_tabs_;
  bool restoring_=false;
  bool backup_requested_=false;
  int drag_tab_=-1;POINT drag_start_={};
  unsigned displayed_blocked_=0;
  bool chrome_tabs_=false;
  bool native_chrome_=false;
  bool reparent_chrome_=false;
  bool integrated_chrome_=false;
  std::unique_ptr<EngineWindows> engine_windows_;
  HICON engine_icon_=nullptr;
  bool test_file_dialog_=false;
  bool fullscreen_=false;
  LONG_PTR window_style_=0;
  WINDOWPLACEMENT window_placement_={sizeof(WINDOWPLACEMENT)};
  std::string pending_engine_url_;
  int active_=0,next_id_=1,smoke_stage_=0; bool closing_=false,smoke_pass_=false,chrome_probe_=false;
  HINSTANCE instance_=nullptr;
};
}
