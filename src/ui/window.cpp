#include "ui/window.h"
#include "ui/native_chrome_host.h"
#include "ui/credential_window.h"
#include "ui/filter_subscription_window.h"
#include "ui/update_window.h"
#include "update/worker.h"
#include "privacy/tor_browser.h"
#include "browser/client.h"
#include "include/cef_app.h"
#include "include/cef_parser.h"
#include "include/cef_id_mappers.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <commdlg.h>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <future>
#include "turan_version.h"

namespace turan {
namespace {
enum { Back=100,Forward,Reload,New,CloseTab,Private,Bookmark,ShieldButton,Menu,
       Bookmarks=200,History,Downloads,Permissions,SettingsPage,Credits,
       PermissionLog=206,
       BackupProfile=207,
       ShieldPage=208,CustomFilters=209,TorBrowserPage=210,
       Passwords=213,Extensions=214,Subscriptions=215,UpdatePage=216,
       Reopen=220,Pin,Mute,GroupWork,GroupPersonal,GroupResearch,Ungroup,
       Home=240,FindPage,ZoomIn,ZoomOut,ZoomReset,FullScreen,Print,SavePDF,DevTools,Stop };
struct PanelData {Window* owner;int kind;int browser_id=-1;std::wstring query;std::vector<PermissionRule> permission_rows;std::future<std::pair<bool,std::string>> filter_job;
  std::string tor_url;bool tor_private=false,tor_launch=false;std::filesystem::path tor_root;std::future<TorBrowserResult> tor_job;};
struct PermissionData {
  Window* owner;CefRefPtr<CefBrowser> browser;std::string origin,top;uint32_t kinds;
  bool incognito;uint64_t prompt_id;std::wstring what;std::function<void(bool)> reply;
  bool created=false;
  CefRefPtr<CefFrame> frame;
};
std::string PermissionOrigin(const std::string& url) {
  CefURLParts parts;if(!CefParseURL(url,parts))return {};
  auto value=CefString(&parts.origin).ToString();if(!value.empty()&&value.back()=='/')value.pop_back();
  return PermissionOriginValid(value)?value:std::string();
}
std::wstring PermissionName(uint32_t kind) {
  switch(kind) {
    case MicrophoneAccess:return L"Microphone / Mikrofon";case CameraAccess:return L"Camera / Kamera";
    case LocationAccess:return L"Location / Konum";case NotificationAccess:return L"Notifications / Bildirimler";
    case ClipboardAccess:return L"Clipboard / Pano";default:return L"Screen / Ekran";
  }
}
cef_content_setting_types_t EnginePermissionType(uint32_t kind) {
  switch(kind) {
    case MicrophoneAccess:return CEF_CONTENT_SETTING_TYPE_MEDIASTREAM_MIC;case CameraAccess:return CEF_CONTENT_SETTING_TYPE_MEDIASTREAM_CAMERA;
    case LocationAccess:return CEF_CONTENT_SETTING_TYPE_GEOLOCATION;case NotificationAccess:return CEF_CONTENT_SETTING_TYPE_NOTIFICATIONS;
    default:return CEF_CONTENT_SETTING_TYPE_CLIPBOARD_READ_WRITE;
  }
}
void SetMediaPermission(CefRefPtr<CefBrowser> browser,const std::string& origin,const std::string& top,uint32_t kinds,bool allow) {
  // Chromium subscribes active captures to engine permission state. Updating only
  // Turan's rule table can cause a granted capture to be cancelled as ASK/denied.
  // The browser's own context keeps private decisions in its in-memory profile.
  auto context=browser->GetHost()->GetRequestContext();
  for(uint32_t kind:{MicrophoneAccess,CameraAccess})if(kinds&kind)
    context->SetContentSetting(origin,top,EnginePermissionType(kind),allow?CEF_CONTENT_SETTING_VALUE_ALLOW:CEF_CONTENT_SETTING_VALUE_BLOCK);
}
class ToolClient final : public CefClient,public CefLifeSpanHandler {
 public:
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {return this;}
 private: IMPLEMENT_REFCOUNTING(ToolClient);
};
class PdfResult final : public CefPdfPrintCallback {
 public:
  PdfResult(HWND target,CefRefPtr<CefBrowser> browser):target_(target),browser_(browser){}
  void OnPdfPrintFinished(const CefString&,bool ok) override {
    if(browser_->IsValid()&&IsWindow(target_))PostMessageW(target_,WM_APP+5,ok,0);
  }
 private:
  HWND target_;CefRefPtr<CefBrowser> browser_;IMPLEMENT_REFCOUNTING(PdfResult);
};
class PdfFileChoice final : public CefRunFileDialogCallback {
 public:
  PdfFileChoice(HWND target,CefRefPtr<CefBrowser> browser):target_(target),browser_(browser){}
  void OnFileDialogDismissed(const std::vector<CefString>& paths) override {
    if(paths.size()==1&&browser_->IsValid()&&IsWindow(target_)) {
      CefPdfPrintSettings settings;browser_->GetHost()->PrintToPDF(paths[0],settings,new PdfResult(target_,browser_));
    }
  }
 private:
  HWND target_;CefRefPtr<CefBrowser> browser_;IMPLEMENT_REFCOUNTING(PdfFileChoice);
};
HWND Control(HWND parent,const wchar_t* cls,const wchar_t* text,int id,int x,int y,int w,int h,DWORD style=0) {
  return CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
}
std::wstring GetText(HWND h) {int n=GetWindowTextLengthW(h);std::wstring value(n+1,0);GetWindowTextW(h,value.data(),n+1);value.resize(n);return value;}
}
Window::Window(Store& store,std::filesystem::path dir,std::string url,std::filesystem::path result,bool chrome_probe,bool chrome_tabs,bool native_chrome,bool test_file_dialog,bool reparent_chrome,bool integrated_chrome)
 :store_(store),app_dir_(std::move(dir)),smoke_result_(std::move(result)),initial_url_(std::move(url)),chrome_probe_(chrome_probe),chrome_tabs_(chrome_tabs),native_chrome_(native_chrome),test_file_dialog_(test_file_dialog),reparent_chrome_(reparent_chrome),integrated_chrome_(integrated_chrome) {
  shield=std::make_shared<Shield>();shield->enabled=store.settings.shield;shield->block_cookies=store.settings.third_party_cookies;shield->https_only=store.settings.https_only;
  shield->LoadRules(app_dir_/L"resources"/L"shield-domains.txt",app_dir_/L"resources"/L"public_suffix_list.dat",store.root);
  std::wstring path=(app_dir_/L"resources"/L"start.html").wstring();std::replace(path.begin(),path.end(),L'\\',L'/');
  auto encoded=CefURIEncode(Utf8(path),false).ToString();
  for(auto pair:std::vector<std::pair<std::string,std::string>>{{"%2F","/"},{"%3A",":"}}) {
    size_t pos=0;while((pos=encoded.find(pair.first,pos))!=std::string::npos){encoded.replace(pos,pair.first.size(),pair.second);pos+=pair.second.size();}
  }
  start_url_="file:///"+encoded;
  CefURLParts parts;if(CefParseURL(start_url_,parts))start_url_=CefString(&parts.spec);
  uint64_t index=0;
  for(auto& saved:store_.downloads) {
    Download download;download.name=saved.name;download.path=saved.path;download.canceled=saved.canceled;
    download.complete=!saved.canceled;download.percent=download.complete?100:-1;download.recorded=true;
    downloads_.emplace(++index,std::move(download));
  }
}
std::wstring Window::Text(const wchar_t* en,const wchar_t* tr) const {return store_.settings.turkish?tr:en;}
HWND Window::UiOwner() const {
  if(integrated_chrome_&&active_>=0&&active_<static_cast<int>(tabs_.size())&&tabs_[active_].browser) {
    auto frame=tabs_[active_].browser->GetHost()->GetWindowHandle();if(IsWindow(frame))return frame;
  }
  return hwnd_;
}
bool Window::Create(HINSTANCE instance) {
  instance_=instance;INITCOMMONCONTROLSEX common={sizeof(common),ICC_TAB_CLASSES};InitCommonControlsEx(&common);
  font_=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
  background_=CreateSolidBrush(store_.settings.dark?RGB(16,25,35):RGB(245,247,250));
  WNDCLASSW cls={};cls.hInstance=instance;cls.lpszClassName=L"TuranWindow";cls.lpfnWndProc=Proc;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=static_cast<HICON>(LoadImageW(nullptr,(app_dir_/L"resources/turan.ico").c_str(),IMAGE_ICON,32,32,LR_LOADFROMFILE));RegisterClassW(&cls);
  hwnd_=CreateWindowW(cls.lpszClassName,L"Turan Browser",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1200,800,nullptr,nullptr,instance,this);
  if(!hwnd_)return false;
  if(integrated_chrome_) {
    engine_icon_=static_cast<HICON>(LoadImageW(nullptr,(app_dir_/L"resources/turan.ico").c_str(),IMAGE_ICON,32,32,LR_LOADFROMFILE));
    engine_windows_=std::make_unique<EngineWindows>([this](HWND frame,int command) {
      // Select a frame's already-active tab via the last focus event. No engine
      // activation is performed inside Chromium's non-reentrant tab callbacks.
      if(!Active()||!Active()->browser||Active()->browser->GetHost()->GetWindowHandle()!=frame)
        for(size_t i=0;i<tabs_.size();++i)if(tabs_[i].browser&&tabs_[i].browser->GetHost()->GetWindowHandle()==frame){active_=static_cast<int>(i);break;}
      Command(command);
    });
  }
  tabs_control_=Control(hwnd_,WC_TABCONTROLW,L"",10,150,12,800,34,TCS_OWNERDRAWFIXED|TCS_FIXEDWIDTH|TCS_SINGLELINE);
  TabCtrl_SetItemSize(tabs_control_,170,32);
  SetWindowSubclass(tabs_control_,TabProc,1,reinterpret_cast<DWORD_PTR>(this));
  for(auto id:{Back,Forward,Reload,New,CloseTab,Bookmark,ShieldButton,Menu}) {
    auto b=Control(hwnd_,L"BUTTON",L"",id,0,0,32,32,BS_OWNERDRAW);SendMessageW(b,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);buttons_.push_back(b);
  }
  omnibox_=Control(hwnd_,L"EDIT",L"",20,130,67,650,32,ES_AUTOHSCROLL|WS_BORDER);
  SendMessageW(omnibox_,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);
  SetWindowSubclass(omnibox_,EditProc,1,reinterpret_cast<DWORD_PTR>(this));
  status_=Control(hwnd_,L"STATIC",L"",21,16,740,1000,25);SendMessageW(status_,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);
  normal_=CefRequestContext::GetGlobalContext();
  Layout();Refresh();ShowWindow(hwnd_,is_smoke()||integrated_chrome_?SW_HIDE:SW_SHOW);UpdateWindow(hwnd_);
  SetTimer(hwnd_,1,500,nullptr);
  try {
    auto profile=store_.root;auto suffix=shield->Suffix();
    subscription_initialization_=std::async(std::launch::async,[profile,suffix](){return std::make_shared<FilterSubscriptions>(profile,suffix);});
  }catch(...){subscription_status_="Subscription worker unavailable; starter/custom policy retained";}
  SessionState session;
  bool previous=SessionJournal(store_.root).Load(session);
  restoring_=true;
  if(!is_smoke()&&!chrome_probe_&&initial_url_.empty()&&previous&&store_.settings.restore_session&&!session.tabs.empty()) {
    closed_tabs_=session.closed;
    for(auto& saved:session.tabs) {
      NewTab(false,saved.url);auto tab=Active();
      if(tab){tab->title=saved.title;tab->pinned=saved.pinned;tab->muted=saved.muted;tab->group=saved.group;tab->color=saved.color;}
    }
    Select(static_cast<int>(session.active));
    if(!session.clean)SetWindowTextW(status_,Text(L"Recovered tabs after an interrupted session",L"Kesilen oturumun sekmeleri kurtarıldı").c_str());
  } else NewTab(false,AllowedNavigation(initial_url_)?initial_url_:"");
  restoring_=false;SaveSession();
  if(store_.recovered())SetWindowTextW(status_,Text(L"Profile settings, history and bookmarks recovered from the verified backup",L"Profil ayarları, geçmiş ve yer imleri doğrulanmış yedekten kurtarıldı").c_str());
  return true;
}
void Window::PollSubscriptions() {
  if(closing_)return;
  if(subscription_initialization_.valid()&&subscription_initialization_.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
    try{subscriptions_=subscription_initialization_.get();}catch(...){subscription_status_="Subscription initialization failed; previous files preserved";}
  }
  if(!subscriptions_)return;
  subscriptions_->RefreshAsync();
  if(subscription_publication_.valid()) {
    if(subscription_publication_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
    try {auto result=subscription_publication_.get();subscription_seen_revision_=result.first;
      subscription_status_=result.second.empty()?"Validated subscription policy is active":("Previous policy retained: "+result.second);
    }catch(...){subscription_status_="Policy publication failed; previous policy retained";subscription_seen_revision_=subscriptions_->Revision();}
  }
  auto revision=subscriptions_->Revision();
  if(!subscription_force_&&revision==subscription_seen_revision_)return;
  subscription_force_=false;auto manager=subscriptions_;auto policy=shield;
  try {
    subscription_status_="Compiling filter policy; previous policy remains active";
    subscription_publication_=std::async(std::launch::async,[manager,policy,revision](){
      auto sources=manager->Sources();std::string error;policy->SetSubscriptions(sources,error);return std::make_pair(revision,error);
    });
  }catch(...){subscription_status_="Policy worker unavailable; previous policy retained";subscription_seen_revision_=revision;}
}
Tab* Window::Find(int id) {for(auto& t:tabs_)if(t.id==id)return &t;return nullptr;}
Tab* Window::Active() {return active_>=0&&active_<static_cast<int>(tabs_.size())?&tabs_[active_]:nullptr;}
void Window::NewTab(bool incognito,const std::string& url) {
  if(closing_)return;
  if(tabs_.size()>=256){MessageBoxW(hwnd_,L"The 256-tab safety limit has been reached.",L"Turan Browser",MB_OK);return;}
  if(chrome_tabs_&&(!incognito||integrated_chrome_)) {
    auto existing=std::find_if(tabs_.begin(),tabs_.end(),[incognito](const Tab& t){return t.private_tab==incognito&&t.browser;});
    if(integrated_chrome_&&Active()&&Active()->private_tab==incognito&&Active()->browser)
      existing=tabs_.begin()+active_;
    if(existing!=tabs_.end()) {
      pending_engine_url_=url.empty()?start_url_:url;
      existing->browser->GetHost()->ExecuteChromeCommand(cef_id_for_command_id_name("IDC_NEW_TAB"),CEF_WOD_CURRENT_TAB);
      return;
    }
  }
  if(incognito&&!private_) {CefRequestContextSettings config;private_=CefRequestContext::CreateContext(config,nullptr);}
  int id=next_id_++;tabs_.push_back({id,incognito});active_=static_cast<int>(tabs_.size())-1;
  tabs_.back().url=url.empty()?start_url_:url;
  TCITEMW item={};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t*>(L"New tab");TabCtrl_InsertItem(tabs_control_,active_,&item);
  CefWindowInfo info;if(!native_chrome_)info.SetAsChild(hwnd_,CefRect(0,112,1100,600));info.runtime_style=chrome_probe_?CEF_RUNTIME_STYLE_CHROME:CEF_RUNTIME_STYLE_ALLOY;
  CefBrowserSettings browser_settings;
  CefRefPtr<Client> client=new Client(this,id,incognito);
  if(!CefBrowserHost::CreateBrowser(info,client,url.empty()?start_url_:url,browser_settings,nullptr,incognito?private_:normal_)) {
    tabs_.pop_back();TabCtrl_DeleteItem(tabs_control_,active_);active_=std::max(0,active_-1);
    MessageBoxW(hwnd_,L"CEF could not create a tab.",L"Turan Browser",MB_ICONERROR);if(tabs_.empty())Close();return;
  }
  Select(active_);SaveSession();
}
int Window::BrowserTab(CefRefPtr<CefBrowser> browser) const {
  for(const auto& tab:tabs_)if(tab.browser&&tab.browser->GetIdentifier()==browser->GetIdentifier())return tab.id;
  return -1;
}
bool Window::IsPrivateBrowser(CefRefPtr<CefBrowser> browser) const {
  for(const auto& tab:tabs_)if(tab.browser&&tab.browser->GetIdentifier()==browser->GetIdentifier())return tab.private_tab;
  return true; // Unknown/closing browsers must not write to the normal profile.
}
void Window::Created(int id,CefRefPtr<CefBrowser> browser) {
  auto t=Find(id);
  // A real Chromium tab model may reuse its initial CefClient for new tabs.
  if(native_chrome_&&t&&t->browser&&t->browser->GetIdentifier()!=browser->GetIdentifier()) {
    id=next_id_++;tabs_.push_back({id,false});t=&tabs_.back();
    t->url=pending_engine_url_;t->engine_owned=!pending_engine_url_.empty();pending_engine_url_.clear();
    active_=static_cast<int>(tabs_.size())-1;
    TCITEMW item={};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t*>(L"New tab");TabCtrl_InsertItem(tabs_control_,active_,&item);
  }
  if(!t){browser->GetHost()->CloseBrowser(true);return;}t->browser=browser;browser->GetHost()->SetAudioMuted(t->muted);
  if(reparent_chrome_) {
    // A reused/default client is not a reliable source of incognito identity.
    t->private_tab=!browser->GetHost()->GetRequestContext()->IsSharingWith(normal_);
    std::wstring error;
    if(!NativeChromeHost::Attach(browser->GetHost()->GetWindowHandle(),hwnd_,error)) {
      MessageBoxW(hwnd_,error.c_str(),L"Turan Browser - native Chrome probe",MB_OK|MB_ICONERROR);
      Close();return;
    }
  }
  if(integrated_chrome_) {
    t->private_tab=!browser->GetHost()->GetRequestContext()->IsSharingWith(normal_);
    std::wstring error;
    if(!engine_windows_->Bind(browser->GetHost()->GetWindowHandle(),engine_icon_,error)) {
      MessageBoxW(UiOwner(),error.c_str(),L"Turan Browser - integrated frame",MB_OK|MB_ICONERROR);Close();return;
    }
  }
  if(closing_)browser->GetHost()->CloseBrowser(true);else {if(integrated_chrome_)PostMessageW(hwnd_,WM_APP+6,id,0);else Select(active_);if(reparent_chrome_)PostMessageW(hwnd_,WM_APP+7,0,0);if(t->engine_owned)PostMessageW(hwnd_,WM_APP+3,id,0);}
}
CefRefPtr<CefClient> Window::EngineClient() {
  if(closing_)return nullptr;
  int id=next_id_++;tabs_.push_back({id,false});active_=static_cast<int>(tabs_.size())-1;
  tabs_.back().url=pending_engine_url_;tabs_.back().engine_owned=!pending_engine_url_.empty();pending_engine_url_.clear();
  TCITEMW item={};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t*>(L"New tab");TabCtrl_InsertItem(tabs_control_,active_,&item);
  return new Client(this,id,false);
}
void Window::EngineActivated(CefRefPtr<CefBrowser> browser) {
  if((!reparent_chrome_&&!integrated_chrome_)||closing_)return;
  for(size_t i=0;i<tabs_.size();++i)if(tabs_[i].browser&&tabs_[i].browser->IsSame(browser)) {
    // TabStripModel is not reentrant. In particular, Select->SetFocus from this
    // callback can re-enter the extension's pending tabs.update operation.
    PostMessageW(hwnd_,WM_APP+6,tabs_[i].id,0);return;
  }
}
void Window::Closed(int id) {
  auto it=std::find_if(tabs_.begin(),tabs_.end(),[&](const Tab& t){return t.id==id;});if(it==tabs_.end())return;
  int index=static_cast<int>(it-tabs_.begin());
  if(!closing_&&!it->private_tab&&AllowedNavigation(it->url)&&it->url!="about:blank") {
    closed_tabs_.push_back({it->title,it->url,it->group,it->pinned,it->muted,it->color});
    if(closed_tabs_.size()>20)closed_tabs_.erase(closed_tabs_.begin());
  }
  tabs_.erase(it);TabCtrl_DeleteItem(tabs_control_,index);if(index<active_)--active_;
  bool any_private=std::any_of(tabs_.begin(),tabs_.end(),[](const Tab& t){return t.private_tab;});if(!any_private){private_=nullptr;private_permissions_.clear();}
  if(tabs_.empty()) {if(integrated_chrome_&&!closing_)Close();if(closing_)PostMessageW(hwnd_,WM_APP+2,0,0);else NewTab();}
  else {active_=std::min(active_,static_cast<int>(tabs_.size())-1);Select(active_);}
  if(!closing_)SaveSession();
}
void Window::Select(int index) {
  if(index<0||index>=static_cast<int>(tabs_.size()))return;active_=index;TabCtrl_SetCurSel(tabs_control_,index);
  if(integrated_chrome_) {if(tabs_[index].browser)tabs_[index].browser->GetHost()->SetFocus(true);Refresh();SaveSession();return;}
  std::map<HWND,bool> visibility;
  for(int i=0;i<static_cast<int>(tabs_.size());++i)if(tabs_[i].browser) {
    auto handle=tabs_[i].browser->GetHost()->GetWindowHandle();visibility[handle]=visibility[handle]||i==index;
  }
  for(auto& pair:visibility)ShowWindow(pair.first,pair.second?SW_SHOW:SW_HIDE);
  if(tabs_[index].browser) {
    if(reparent_chrome_)NativeChromeHost::ActivateWithinOwner(tabs_[index].browser->GetHost()->GetWindowHandle(),hwnd_);
    tabs_[index].browser->GetHost()->SetFocus(true);
  }
  Layout();Refresh();SaveSession();
}
void Window::Layout() {
  if(!hwnd_)return;RECT r;GetClientRect(hwnd_,&r);int w=r.right,h=r.bottom;
  for(HWND child:buttons_)ShowWindow(child,fullscreen_?SW_HIDE:SW_SHOW);
  for(HWND child:{tabs_control_,omnibox_,status_})ShowWindow(child,fullscreen_?SW_HIDE:SW_SHOW);
  MoveWindow(tabs_control_,150,12,std::max(50,w-254),36,TRUE);
  const int xs[]={16,52,88,w-94,w-50,w-258,w-214,w-88};const int ys[]={66,66,66,12,12,66,66,66};const int ws[]={32,32,32,36,36,36,118,72};
  for(size_t i=0;i<buttons_.size();++i)MoveWindow(buttons_[i],xs[i],ys[i],ws[i],32,TRUE);
  MoveWindow(omnibox_,132,67,std::max(100,w-398),30,TRUE);MoveWindow(status_,16,h-26,std::max(100,w-32),23,TRUE);
  if(!native_chrome_)for(auto& t:tabs_)if(t.browser) {MoveWindow(t.browser->GetHost()->GetWindowHandle(),0,fullscreen_?0:112,w,std::max(1,fullscreen_?h:h-140),TRUE);t.browser->GetHost()->NotifyMoveOrResizeStarted();}
  if(reparent_chrome_) {
    std::map<HWND,CefRefPtr<CefBrowser>> frames;
    for(auto& tab:tabs_)if(tab.browser)frames.emplace(tab.browser->GetHost()->GetWindowHandle(),tab.browser);
    RECT bounds={0,fullscreen_?0:112,w,fullscreen_?h:std::max(113,h-28)};
    for(auto& frame:frames)if(NativeChromeHost::Layout(frame.first,hwnd_,bounds))frame.second->GetHost()->NotifyMoveOrResizeStarted();
  }
}
void Window::Refresh() {
  if(!hwnd_)return;
  const wchar_t* labels[]={L"‹",L"›",L"↻",L"+",L"×",L"☆",L"Shield",L"☰"};
  for(size_t i=0;i<buttons_.size();++i)SetWindowTextW(buttons_[i],labels[i]);
  auto t=Active();
  bool protecting=shield->enabled&&shield->Report().ok&&(!t||!t->browser||shield->SiteEnabled(t->browser->GetIdentifier(),t->url));
  SetWindowTextW(GetDlgItem(hwnd_,ShieldButton),(Text(protecting?L"Shield ON":L"Shield OFF",protecting?L"Kalkan AÇIK":L"Kalkan KAPALI")).c_str());
  if(t) {if(GetFocus()!=omnibox_)SetWindowTextW(omnibox_,Wide(t->url==start_url_?"":t->url).c_str());
    SetWindowTextW(GetDlgItem(hwnd_,Reload),t->loading?L"■":L"↻");
    auto caption=Wide(t->title)+(t->private_tab?L" • Private":L"")+L" — Turan Browser";SetWindowTextW(hwnd_,caption.c_str());
    EnableWindow(GetDlgItem(hwnd_,Back),t->browser&&t->browser->CanGoBack());EnableWindow(GetDlgItem(hwnd_,Forward),t->browser&&t->browser->CanGoForward());
    bool marked=std::any_of(store_.bookmarks.begin(),store_.bookmarks.end(),[&](const Entry& e){return e.url==t->url;});SetWindowTextW(GetDlgItem(hwnd_,Bookmark),marked?L"★":L"☆");
    EnableWindow(GetDlgItem(hwnd_,Bookmark),AllowedNavigation(t->url)&&t->url!="about:blank");
    EnableWindow(GetDlgItem(hwnd_,CloseTab),t->browser!=nullptr);
    EnableWindow(omnibox_,t->browser!=nullptr);
  }
  if(integrated_chrome_&&t&&t->browser&&engine_windows_) {
    engine_windows_->Brand(t->browser->GetHost()->GetWindowHandle(),Wide(t->title)+(t->private_tab?L" • Private":L""));
  }
  BOOL dark=store_.settings.dark;DwmSetWindowAttribute(hwnd_,20,&dark,sizeof(dark));
  std::wstring line=(t&&t->private_tab?Text(L"Private tab • History and cookies stay in memory",L"Gizli sekme • Geçmiş ve çerezler bellekte tutulur"):Text(L"Turan Browser",L"Turan Browser"));
  auto events=t&&t->browser?shield->Events(t->browser->GetIdentifier()):Shield::Snapshot{};
  displayed_blocked_=static_cast<unsigned>(events.ads+events.trackers+events.custom);
  line+=L"    •    "+Text(L"Blocked requests: ",L"Engellenen istek: ")+std::to_wstring(displayed_blocked_);
  if(t&&t->loading)line+=Text(L"    •    Loading…",L"    •    Yükleniyor…");SetWindowTextW(status_,line.c_str());
  InvalidateRect(hwnd_,nullptr,TRUE);InvalidateRect(tabs_control_,nullptr,TRUE);
}
void Window::Address(int id,const std::string& url) {auto t=Find(id);if(t)t->url=url;if(t==Active())Refresh();SaveSession();}
void Window::Title(int id,const std::string& title) {auto t=Find(id);if(t)t->title=title;Refresh();SaveSession();
  if(is_smoke()&&title=="TURAN_SMOKE_OK"&&smoke_stage_==0) {smoke_stage_=1;if(t){store_.Bookmark({t->title,t->url});Save();}NewTab(true,Origin(initial_url_)+"/private");}
  else if(is_smoke()&&title=="PRIVATE_OK"&&smoke_stage_==1) {smoke_stage_=2;if(t&&t->browser)t->browser->GetMainFrame()->LoadURL(Origin(initial_url_)+"/private-download");}
  else if(is_smoke()&&title=="NORMAL_OK"&&smoke_stage_==3) {smoke_stage_=4;auto active=Active();if(active&&active->browser)active->browser->GetMainFrame()->LoadURL(Origin(initial_url_)+"/download");}}
void Window::Loading(int id,bool loading) {auto t=Find(id);if(t)t->loading=loading;Refresh();}
void Window::Loaded(int id) {auto t=Find(id);if(t){store_.Visit({t->title,t->url},t->private_tab);if(!t->private_tab)Save();}}
void Window::Error(int id,const std::string& text) {auto t=Find(id);if(t==Active())SetWindowTextW(status_,(L"Navigation failed: "+Wide(text)).c_str());}
void Window::Save() {if(!store_.Save())SetWindowTextW(status_,L"Profile write failed. Check folder permissions.");}
void Window::SaveSession(bool clean) {
  if(is_smoke()||chrome_probe_||restoring_||(closing_&&!clean))return;
  SessionState state;state.clean=clean;state.closed=closed_tabs_;
  for(size_t i=0;i<tabs_.size();++i) {
    const auto& t=tabs_[i];if(t.private_tab||!AllowedNavigation(t.url)||t.url=="about:blank")continue;
    if(static_cast<int>(i)==active_)state.active=static_cast<unsigned>(state.tabs.size());
    state.tabs.push_back({t.title,t.url,t.group,t.pinned,t.muted,t.color});
  }
  if(!SessionJournal(store_.root).Save(state))SetWindowTextW(status_,Text(L"Session write failed",L"Oturum kaydedilemedi").c_str());
}
void Window::RebuildTabStrip() {
  TabCtrl_DeleteAllItems(tabs_control_);
  for(size_t i=0;i<tabs_.size();++i){TCITEMW item={};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t*>(L"Tab");TabCtrl_InsertItem(tabs_control_,static_cast<int>(i),&item);}
  Select(active_);
}
void Window::MoveTab(int from,int to) {
  if(from<0||to<0||from>=static_cast<int>(tabs_.size())||to>=static_cast<int>(tabs_.size())||from==to)return;
  if(tabs_[from].pinned!=tabs_[to].pinned)return;
  int selected=Active()?Active()->id:-1;
  auto tab=std::move(tabs_[from]);tabs_.erase(tabs_.begin()+from);tabs_.insert(tabs_.begin()+to,std::move(tab));
  for(size_t i=0;i<tabs_.size();++i)if(tabs_[i].id==selected)active_=static_cast<int>(i);
  RebuildTabStrip();SaveSession();
}
void Window::ReopenTab() {
  if(closed_tabs_.empty())return;
  auto saved=closed_tabs_.back();closed_tabs_.pop_back();NewTab(false,saved.url);
  auto t=Active();if(t){t->title=saved.title;t->pinned=saved.pinned;t->muted=saved.muted;t->group=saved.group;t->color=saved.color;if(t->browser)t->browser->GetHost()->SetAudioMuted(t->muted);}
  int id=t?t->id:-1;std::stable_partition(tabs_.begin(),tabs_.end(),[](const Tab& tab){return tab.pinned;});
  for(size_t i=0;i<tabs_.size();++i)if(tabs_[i].id==id)active_=static_cast<int>(i);
  RebuildTabStrip();SaveSession();
}
void Window::NavigateBar() {auto t=Active();if(t&&t->browser){auto url=Navigate(Utf8(GetText(omnibox_)),shield->https_only);t->browser->GetMainFrame()->LoadURL(url);t->browser->GetHost()->SetFocus(true);}}
void Window::FindResult(int id,int count,int ordinal) {
  for(HWND panel:panels_) {
    auto data=reinterpret_cast<PanelData*>(GetWindowLongPtrW(panel,GWLP_USERDATA));
    if(data&&data->kind==FindPage&&data->browser_id==id)
      SetWindowTextW(GetDlgItem(panel,65),(std::to_wstring(ordinal)+L" / "+std::to_wstring(count)).c_str());
  }
}
void Window::PageFullscreen(int id,bool fullscreen) {if(Active()&&Active()->id==id)Fullscreen(fullscreen);}
void Window::Fullscreen(bool enabled) {
  if(fullscreen_==enabled||(native_chrome_&&!reparent_chrome_))return;
  fullscreen_=enabled;
  if(enabled) {
    window_style_=GetWindowLongPtrW(hwnd_,GWL_STYLE);GetWindowPlacement(hwnd_,&window_placement_);
    MONITORINFO monitor={sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(hwnd_,MONITOR_DEFAULTTONEAREST),&monitor);
    SetWindowLongPtrW(hwnd_,GWL_STYLE,window_style_&~WS_OVERLAPPEDWINDOW);
    SetWindowPos(hwnd_,nullptr,monitor.rcMonitor.left,monitor.rcMonitor.top,monitor.rcMonitor.right-monitor.rcMonitor.left,monitor.rcMonitor.bottom-monitor.rcMonitor.top,SWP_NOZORDER|SWP_FRAMECHANGED);
  } else {
    SetWindowLongPtrW(hwnd_,GWL_STYLE,window_style_);SetWindowPlacement(hwnd_,&window_placement_);
    SetWindowPos(hwnd_,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);
  }
  Layout();
}
bool Window::Shortcut(int key,bool ctrl,bool shift) {
  if(integrated_chrome_) {
    if(ctrl&&shift&&key=='M'){Command(Menu);return true;}
    // These commands must use the engine's genuine tab/window/omnibox model.
    if((ctrl&&(key=='L'||key=='T'||key=='N'||key=='W'))||key==VK_F11||key==VK_ESCAPE)return false;
  }
  if(ctrl&&key=='L'){SetFocus(omnibox_);SendMessageW(omnibox_,EM_SETSEL,0,-1);return true;}
  if(ctrl&&shift&&key=='T'){ReopenTab();return true;}
  if(ctrl&&key=='T'){NewTab();return true;}
  if(ctrl&&shift&&key=='N'){NewTab(true);return true;}
  if(ctrl&&key=='W'){Command(CloseTab);return true;}
  if(ctrl&&key=='D'){Command(Bookmark);return true;}
  if(ctrl&&key=='H'){Panel(History);return true;}
  if(ctrl&&key=='J'){Panel(Downloads);return true;}
  if(ctrl&&key=='F'){Panel(FindPage);return true;}
  if(ctrl&&(key==VK_OEM_PLUS||key==VK_ADD)){Command(ZoomIn);return true;}
  if(ctrl&&(key==VK_OEM_MINUS||key==VK_SUBTRACT)){Command(ZoomOut);return true;}
  if(ctrl&&key=='0'){Command(ZoomReset);return true;}
  if(ctrl&&key=='P'){Command(Print);return true;}
  if(key==VK_F11){Command(FullScreen);return true;}
  if(key==VK_F12||(ctrl&&shift&&key=='I')){Command(DevTools);return true;}
  if(key==VK_ESCAPE){if(fullscreen_)Fullscreen(false);else Command(Stop);return true;}
  if(key==VK_F5){Command(Reload);return true;}
  return false;
}
void Window::Command(int id) {
  auto t=Active();auto b=t?t->browser:nullptr;
  switch(id) {
    case Back:if(b)b->GoBack();break;case Forward:if(b)b->GoForward();break;case Reload:if(b){if(t->loading)b->StopLoad();else b->Reload();}break;
    case Stop:if(b)b->StopLoad();break;
    case Home:if(b)b->GetMainFrame()->LoadURL(start_url_);break;
    case FindPage:Panel(FindPage);break;
    case ZoomIn:case ZoomOut:case ZoomReset:if(b)b->GetHost()->SetZoomLevel(id==ZoomReset?0.0:std::clamp(b->GetHost()->GetZoomLevel()+(id==ZoomIn?1.0:-1.0),-7.0,9.0));break;
    case FullScreen:if(integrated_chrome_&&b)b->GetHost()->ExecuteChromeCommand(cef_id_for_command_id_name("IDC_FULLSCREEN"),CEF_WOD_CURRENT_TAB);else Fullscreen(!fullscreen_);break;
    case Print:if(b)b->GetHost()->Print();break;
    case SavePDF:if(b)b->GetHost()->RunFileDialog(FILE_DIALOG_SAVE,Text(L"Save PDF",L"PDF olarak kaydet"),"page.pdf",{".pdf"},new PdfFileChoice(hwnd_,b));break;
    case DevTools:if(b){if(b->GetHost()->HasDevTools())b->GetHost()->CloseDevTools();else {CefWindowInfo info;info.SetAsPopup(hwnd_,"Turan Developer Tools");CefBrowserSettings config;b->GetHost()->ShowDevTools(info,new ToolClient,config,CefPoint());}}break;
    case New:NewTab();break;case Private:NewTab(true);break;case CloseTab:if(b)b->GetHost()->CloseBrowser(false);break;
    case Reopen:ReopenTab();break;
    case Pin:if(t){int selected=t->id;t->pinned=!t->pinned;std::stable_partition(tabs_.begin(),tabs_.end(),[](const Tab& tab){return tab.pinned;});for(size_t i=0;i<tabs_.size();++i)if(tabs_[i].id==selected)active_=static_cast<int>(i);RebuildTabStrip();SaveSession();}break;
    case Mute:if(t){t->muted=!t->muted;if(b)b->GetHost()->SetAudioMuted(t->muted);SaveSession();Refresh();}break;
    case GroupWork:case GroupPersonal:case GroupResearch:case Ungroup:
      if(t){t->group=id==GroupWork?"Work":id==GroupPersonal?"Personal":id==GroupResearch?"Research":"";t->color=id==GroupWork?1:id==GroupPersonal?2:id==GroupResearch?3:0;SaveSession();Refresh();}break;
    case Bookmark:if(t){store_.Bookmark({t->title,t->url});Save();Refresh();}break;
    case ShieldButton:Panel(ShieldPage);break;
    case Menu:{HMENU menu=CreatePopupMenu();
      for(auto row:std::vector<std::pair<int,std::wstring>>{{New,Text(L"New tab\tCtrl+T",L"Yeni sekme\tCtrl+T")},{Private,Text(L"Private tab\tCtrl+Shift+N",L"Gizli sekme\tCtrl+Shift+N")},{Bookmarks,Text(L"Bookmarks",L"Yer imleri")},{History,Text(L"History\tCtrl+H",L"Geçmiş\tCtrl+H")},{Downloads,Text(L"Downloads\tCtrl+J",L"İndirmeler\tCtrl+J")},{Permissions,Text(L"Permissions",L"İzinler")},{SettingsPage,Text(L"Settings & Turan Shield",L"Ayarlar ve Turan Kalkanı")},{Credits,L"About & licenses"}})AppendMenuW(menu,MF_STRING,row.first,row.second.c_str());
      AppendMenuW(menu,closed_tabs_.empty()?MF_GRAYED:MF_STRING,Reopen,Text(L"Reopen closed tab\tCtrl+Shift+T",L"Kapatılan sekmeyi aç\tCtrl+Shift+T").c_str());
      AppendMenuW(menu,MF_STRING,BackupProfile,Text(L"Back up full profile and close",L"Tüm profili yedekle ve kapat").c_str());
      AppendMenuW(menu,MF_STRING,TorBrowserPage,Text(L"Open this page in Tor Browser…",L"Bu sayfayı Tor Browser ile aç…").c_str());
      AppendMenuW(menu,MF_STRING,Passwords,Text(L"Password vault…",L"Parola kasası…").c_str());
      AppendMenuW(menu,MF_STRING,Subscriptions,Text(L"Shield filter subscriptions…",L"Kalkan filtre abonelikleri…").c_str());
      AppendMenuW(menu,MF_STRING,UpdatePage,Text(L"Secure updates…",L"Güvenli güncellemeler…").c_str());
      if(integrated_chrome_)AppendMenuW(menu,MF_STRING,Extensions,Text(L"Extensions…",L"Eklentiler…").c_str());
      AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
      for(auto row:std::vector<std::pair<int,std::wstring>>{{Home,Text(L"Home",L"Ana sayfa")},{FindPage,Text(L"Find in page\tCtrl+F",L"Sayfada bul\tCtrl+F")},{ZoomIn,Text(L"Zoom in\tCtrl++",L"Yakınlaştır\tCtrl++")},{ZoomOut,Text(L"Zoom out\tCtrl+-",L"Uzaklaştır\tCtrl+-")},{ZoomReset,Text(L"Reset zoom\tCtrl+0",L"Yakınlaştırmayı sıfırla\tCtrl+0")},{FullScreen,Text(L"Fullscreen\tF11",L"Tam ekran\tF11")},{Print,Text(L"Print\tCtrl+P",L"Yazdır\tCtrl+P")},{SavePDF,Text(L"Save PDF",L"PDF olarak kaydet")},{DevTools,Text(L"Developer tools\tF12",L"Geliştirici araçları\tF12")}})AppendMenuW(menu,MF_STRING,row.first,row.second.c_str());
      POINT p;GetCursorPos(&p);int chosen=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTALIGN,p.x,p.y,0,UiOwner(),nullptr);DestroyMenu(menu);if(chosen)Command(chosen);break;}
    case BackupProfile:backup_requested_=true;Close();break;
    case Passwords: {
      for(HWND panel:credential_windows_)if(IsWindow(panel)) {
        wchar_t name[64]={};GetClassNameW(panel,name,64);
        if(std::wstring(name)==L"TuranCredentialWindow"){SetForegroundWindow(panel);return;}
      }
      // A manager owns its own locked vault; private tabs need explicit native
      // save consent and never automatically write credentials to this profile.
      auto panel=OpenCredentialWindow(UiOwner(),store_.root,store_.settings.turkish,
        [this](){auto current=Active();return current?current->browser:CefRefPtr<CefBrowser>();},
        [this](CefRefPtr<CefBrowser> current){return IsPrivateBrowser(current);});
      if(panel)credential_windows_.push_back(panel);break;
    }
    case Extensions:if(integrated_chrome_)NewTab(false,"chrome://extensions/");break;
    case Subscriptions: {
      if(!subscriptions_){MessageBoxW(UiOwner(),Wide(subscription_status_).c_str(),L"Turan Shield",MB_OK);break;}
      auto panel=OpenFilterSubscriptionWindow(UiOwner(),subscriptions_,store_.settings.turkish,
        [this](){subscription_force_=true;},[this](){return subscription_status_;});
      if(panel)subscription_windows_.push_back(panel);break;
    }
    case UpdatePage: {
      auto panel=ShowUpdateWindow(UiOwner(),app_dir_,store_.settings.turkish,[this](const std::filesystem::path& stage){
        std::string error;if(update::StartWorker(app_dir_,stage,error))Close();
        else MessageBoxW(UiOwner(),Wide(error).c_str(),L"Turan Browser - secure updater",MB_OK|MB_ICONERROR);
      });
      if(panel)update_windows_.push_back(panel);break;
    }
    case Bookmarks:case History:case Downloads:case Permissions:case PermissionLog:case SettingsPage:case TorBrowserPage:Panel(id);break;
    case Credits:MessageBoxW(hwnd_,(L"Turan Browser "+Wide(TURAN_VERSION)+L"\nNorthByte Norway Tec Ltd.\nCEF "+Wide(TURAN_ENGINE_VERSION)+L"\n\nLicenses: LICENSE.txt, CEF-LICENSE.txt, CEF-CREDITS.html\n\nCtrl+T New tab • Ctrl+L Address • Ctrl+W Close\nPrivacy and feature details: README.md").c_str(),L"Turan Browser",MB_OK);break;
  }
}
void Window::Close() {
  if(closing_)return;SaveSession(true);closing_=true;CancelPermissions(-1);KillTimer(hwnd_,1);KillTimer(hwnd_,2);Save();
  // Chrome-style close can invoke OnBeforeClose synchronously and erase tabs_.
  // Hold a snapshot so callbacks cannot invalidate the iteration.
  std::vector<CefRefPtr<CefBrowser>> browsers;
  for(auto& t:tabs_)if(t.browser)browsers.push_back(t.browser);
  for(auto& b:browsers){b->GetHost()->CloseDevTools();b->GetHost()->CloseBrowser(true);}
  if(tabs_.empty())PostMessageW(hwnd_,WM_APP+2,0,0);
}
void Window::DownloadUpdated(CefRefPtr<CefBrowser> browser,CefRefPtr<CefDownloadItem> item,CefRefPtr<CefDownloadItemCallback> callback) {
  uint64_t key=(static_cast<uint64_t>(browser->GetIdentifier())<<32)|item->GetId();auto& d=downloads_[key];
  d.name=item->GetSuggestedFileName();d.path=item->GetFullPath();d.percent=item->GetPercentComplete();d.complete=item->IsComplete();d.canceled=item->IsCanceled();d.callback=callback;
  bool incognito=IsPrivateBrowser(browser);
  if((d.complete||d.canceled)&&!d.recorded) {
    d.recorded=true;
    if(!incognito) {
      auto finished=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
      store_.RememberDownload({d.name,item->GetURL(),d.path,static_cast<uint64_t>(std::max<int64_t>(0,item->GetReceivedBytes())),static_cast<uint64_t>(finished),d.canceled},false);
      Save();
    }
  }
  if(is_smoke()&&d.complete&&incognito&&smoke_stage_==2){smoke_stage_=3;NewTab(false,Origin(initial_url_)+"/normalcheck");}
  if(is_smoke()&&d.complete&&!incognito&&smoke_stage_==4&&shield->blocked>0){smoke_pass_=true;SetTimer(hwnd_,2,700,nullptr);}
}
void Window::Permission(CefRefPtr<CefBrowser> browser,const std::string& requesting,uint32_t kinds,const std::wstring& what,std::function<void(bool)> reply,uint64_t prompt_id,CefRefPtr<CefFrame> frame) {
  auto origin=PermissionOrigin(requesting),top=PermissionOrigin(browser->GetMainFrame()->GetURL());
  if(closing_||is_smoke()||origin.empty()||top.empty()||!kinds||(kinds&~63u)){reply(false);return;}
  bool incognito=IsPrivateBrowser(browser);
  auto known=PermissionDecision(incognito?private_permissions_:store_.permissions,origin,top,kinds);
  if(known.has_value()){if(frame)SetMediaPermission(browser,origin,top,kinds,*known);reply(*known);return;}
  if(permission_prompts_.size()>=16){reply(false);return;}
  WNDCLASSW cls={};cls.hInstance=instance_;cls.lpszClassName=L"TuranPermission";cls.lpfnWndProc=PermissionProc;
  cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&cls);
  auto data=new PermissionData{this,browser,origin,top,kinds,incognito,prompt_id,what,std::move(reply)};
  data->frame=frame;
  auto title=Text(L"Site permission",L"Site izni")+L" — Turan Browser";
  HWND prompt=CreateWindowExW(WS_EX_DLGMODALFRAME,cls.lpszClassName,title.c_str(),WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
      CW_USEDEFAULT,CW_USEDEFAULT,680,330,UiOwner(),nullptr,instance_,data);
  if(prompt&&IsWindow(prompt)){data->created=true;permission_prompts_.push_back(prompt);}
  else {auto rejected=std::move(data->reply);delete data;if(rejected)rejected(false);}
}
void Window::DismissPermission(int browser_id,uint64_t prompt_id) {
  auto prompts=permission_prompts_;
  for(auto prompt:prompts) {
    auto data=reinterpret_cast<PermissionData*>(GetWindowLongPtrW(prompt,GWLP_USERDATA));
    if(data&&data->browser->GetIdentifier()==browser_id&&data->prompt_id==prompt_id)DestroyWindow(prompt);
  }
}
void Window::CancelPermissions(int browser_id) {
  auto prompts=permission_prompts_;
  for(auto prompt:prompts) {
    auto data=reinterpret_cast<PermissionData*>(GetWindowLongPtrW(prompt,GWLP_USERDATA));
    if(data&&(browser_id<0||data->browser->GetIdentifier()==browser_id))SendMessageW(prompt,WM_CLOSE,0,0);
  }
}
void Window::CancelFramePermissions(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame> frame) {
  auto prompts=permission_prompts_;
  for(auto prompt:prompts) {
    auto data=reinterpret_cast<PermissionData*>(GetWindowLongPtrW(prompt,GWLP_USERDATA));
    if(data&&data->browser->GetIdentifier()==browser->GetIdentifier()&&data->frame&&data->frame->GetIdentifier()==frame->GetIdentifier())
      SendMessageW(prompt,WM_CLOSE,0,0);
  }
}
LRESULT CALLBACK Window::PermissionProc(HWND h,UINT msg,WPARAM w,LPARAM l) {
  auto data=reinterpret_cast<PermissionData*>(GetWindowLongPtrW(h,GWLP_USERDATA));
  if(msg==WM_NCCREATE){data=static_cast<PermissionData*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(data));}
  if(!data)return DefWindowProcW(h,msg,w,l);auto self=data->owner;
  if(msg==WM_CREATE) {
    auto text=self->Text(L"Requested by: ",L"İsteyen site: ")+Wide(data->origin)+L"\n"+
        self->Text(L"Viewed on: ",L"Açık olan site: ")+Wide(data->top)+L"\n\n"+data->what;
    Control(h,L"STATIC",text.c_str(),0,20,16,630,150);
    auto detail=(data->kinds&ScreenAccess)?self->Text(L"Applies only to this capture request.",L"Yalnızca bu ekran paylaşımı isteği için geçerlidir."):
        data->incognito?self->Text(L"Remembered in memory for this private session only.",L"Yalnızca bu gizli oturum için bellekte saklanır."):
        self->Text(L"Saved for this profile. Change or reset it in Site permissions.",L"Bu profil için kaydedilir. Site izinlerinden değiştirin veya sıfırlayın.");
    Control(h,L"STATIC",detail.c_str(),0,20,176,630,45);
    Control(h,L"BUTTON",self->Text(L"Allow",L"İzin ver").c_str(),70,20,232,140,32);
    Control(h,L"BUTTON",self->Text(L"Block",L"Engelle").c_str(),71,174,232,140,32);
    for(HWND child=GetWindow(h,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(self->font_),TRUE);
    SetFocus(GetDlgItem(h,71));return 0;
  }
  if(msg==WM_CLOSE||(msg==WM_COMMAND&&(LOWORD(w)==70||LOWORD(w)==71))) {
    bool clicked=msg==WM_COMMAND,allow=clicked&&LOWORD(w)==70;
    // Consent belongs to the displayed page; navigation/closure invalidates it.
    bool same=data->browser->IsValid()&&PermissionOrigin(data->browser->GetMainFrame()->GetURL())==data->top;
    if(data->frame)same=same&&data->frame->IsValid()&&PermissionOrigin(data->frame->GetURL())==data->origin;
    allow=allow&&same&&!self->closing_;
    if(clicked&&same&&!self->closing_&&!(data->kinds&ScreenAccess)) {
      if(data->incognito)RememberPermission(self->private_permissions_,data->origin,data->top,data->kinds,allow);
      else if(RememberPermission(self->store_.permissions,data->origin,data->top,data->kinds,allow))self->Save();
    }
    if(clicked&&!data->incognito) {
      self->permissions_.push_back(Wide(data->origin)+L" — "+data->what+(allow?L"Allowed / İzinli":L"Blocked / Engelli"));
      if(self->permissions_.size()>200)self->permissions_.erase(self->permissions_.begin());
    }
    const bool update_media=clicked&&same&&!self->closing_&&data->frame;
    auto browser=data->browser;auto origin=data->origin,top=data->top;auto kinds=data->kinds;
    auto reply=std::move(data->reply);DestroyWindow(h);
    // Engine content-setting changes can synchronously dismiss permission UI.
    // Retain values/references independently before making any engine call.
    if(update_media)SetMediaPermission(browser,origin,top,kinds,allow);
    if(reply)reply(allow);return 0;
  }
  if(msg==WM_NCDESTROY){self->permission_prompts_.erase(std::remove(self->permission_prompts_.begin(),self->permission_prompts_.end(),h),self->permission_prompts_.end());if(data->created)delete data;SetWindowLongPtrW(h,GWLP_USERDATA,0);}
  return DefWindowProcW(h,msg,w,l);
}
void Window::Panel(int kind) {
  WNDCLASSW cls={};cls.hInstance=instance_;cls.lpszClassName=L"TuranPanel";cls.lpfnWndProc=PanelProc;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&cls);
  auto data=new PanelData{this,kind};
  auto active=Active();if(active&&active->browser)data->browser_id=active->browser->GetIdentifier();
  auto title=kind==FindPage?Text(L"Find in page",L"Sayfada bul"):kind==Bookmarks?Text(L"Bookmarks",L"Yer imleri"):kind==History?Text(L"History",L"Geçmiş"):kind==Downloads?Text(L"Downloads",L"İndirmeler"):kind==Permissions?Text(L"Site permissions — normal profile",L"Site izinleri — normal profil"):kind==PermissionLog?Text(L"Permission request log — this session",L"İzin istek kayıtları — bu oturum"):Text(L"Settings & Turan Shield",L"Ayarlar ve Turan Kalkanı");
  if(kind==ShieldPage)title=Text(L"Turan Shield — this page",L"Turan Kalkanı — bu sayfa");
  if(kind==CustomFilters)title=Text(L"Turan Shield — custom ABP rules",L"Turan Kalkanı — özel ABP kuralları");
  if(kind==TorBrowserPage)title=Text(L"Open in official Tor Browser",L"Resmî Tor Browser ile aç");
  HWND panel=CreateWindowW(cls.lpszClassName,(title+L" — Turan Browser").c_str(),WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,740,490,UiOwner(),nullptr,instance_,data);
  if(panel)panels_.push_back(panel);else delete data;
}
void Window::RefreshPanel(HWND panel,int kind) {
  if(kind==CustomFilters||kind==TorBrowserPage)return;
  if(kind==ShieldPage) {
    auto data=reinterpret_cast<PanelData*>(GetWindowLongPtrW(panel,GWLP_USERDATA));
    auto snapshot=shield->Events(data->browser_id);auto report=shield->Report();
    auto tab=std::find_if(tabs_.begin(),tabs_.end(),[&](const Tab& t){return t.browser&&t.browser->GetIdentifier()==data->browser_id;});
    auto state=Text(L"Ads: ",L"Reklam: ")+std::to_wstring(snapshot.ads)+Text(L"  Trackers: ",L"  İzleyici: ")+std::to_wstring(snapshot.trackers)+Text(L"  Custom: ",L"  Özel: ")+std::to_wstring(snapshot.custom);
    state+=L"\r\n"+Text(L"Network / cosmetic rules: ",L"Ağ / kozmetik kural: ")+std::to_wstring(report.network)+L" / "+std::to_wstring(report.cosmetic);
    if(!report.ok||!report.error.empty())state+=L"\r\n"+Wide(report.error);
    else state+=L"\r\n"+Wide(subscription_status_);
    if(report.unsupported||report.invalid)state+=Text(L"\r\nSkipped unsupported / invalid: ",L"\r\nAtlanan desteklenmeyen / geçersiz: ")+std::to_wstring(report.unsupported)+L" / "+std::to_wstring(report.invalid);
    SetWindowTextW(GetDlgItem(panel,80),state.c_str());
    HWND list=GetDlgItem(panel,30);SendMessageW(list,LB_RESETCONTENT,0,0);
    for(const auto& event:snapshot.events) {
      auto row=Wide(event.decision.source)+L" • "+Wide(event.decision.rule)+L" • "+Wide(event.url);
      SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(row.c_str()));
    }
    auto label=tab!=tabs_.end()&&shield->SiteEnabled(data->browser_id,tab->url)?Text(L"Disable for this site",L"Bu site için kapat"):Text(L"Enable for this site",L"Bu site için aç");
    SetWindowTextW(GetDlgItem(panel,81),label.c_str());
    EnableWindow(GetDlgItem(panel,81),tab!=tabs_.end()&&!PublicSuffix::Domain(Host(tab->url)).empty());
    EnableWindow(GetDlgItem(panel,82),tab!=tabs_.end()&&!tab->private_tab);
    return;
  }
  HWND list=GetDlgItem(panel,30);int selected=static_cast<int>(SendMessageW(list,LB_GETCURSEL,0,0));SendMessageW(list,LB_RESETCONTENT,0,0);
  std::optional<PermissionRule> previous_permission;
  if(kind==Permissions) {
    auto data=reinterpret_cast<PanelData*>(GetWindowLongPtrW(panel,GWLP_USERDATA));
    if(selected>=0&&selected<static_cast<int>(data->permission_rows.size()))previous_permission=data->permission_rows[selected];
  }
  if(kind==Bookmarks||kind==History) {auto& entries=kind==Bookmarks?store_.bookmarks:store_.history;for(auto& e:entries){auto line=Wide(e.title)+L"    •    "+Wide(e.url);SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(line.c_str()));}}
  else if(kind==Downloads) {for(auto& pair:downloads_) {auto& d=pair.second;auto line=Wide(d.name)+L"    "+(d.complete?L"Completed":d.canceled?L"Canceled":d.paused?L"Paused":std::to_wstring(d.percent)+L"%")+L"    "+Wide(d.path);SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(line.c_str()));}}
  else if(kind==PermissionLog){for(auto& p:permissions_)SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(p.c_str()));}
  else if(kind==Permissions) {
    auto data=reinterpret_cast<PanelData*>(GetWindowLongPtrW(panel,GWLP_USERDATA));data->permission_rows=store_.permissions;
    auto active=Active();auto origin=active&&!active->private_tab?PermissionOrigin(active->url):std::string();
    for(uint32_t bit=1;bit<=16&&!origin.empty();bit<<=1) {
      bool found=std::any_of(data->permission_rows.begin(),data->permission_rows.end(),[&](const PermissionRule& rule){return rule.origin==origin&&rule.top==origin&&rule.kind==bit;});
      if(!found)data->permission_rows.push_back({origin,origin,bit,false});
    }
    for(auto& rule:data->permission_rows) {
      auto saved=std::find_if(store_.permissions.begin(),store_.permissions.end(),[&](const PermissionRule& item){return item.origin==rule.origin&&item.top==rule.top&&item.kind==rule.kind;});
      auto setting=normal_->GetContentSetting(rule.origin,rule.top,EnginePermissionType(rule.kind));
      auto value=saved!=store_.permissions.end()?(saved->allow?Text(L"Allow",L"İzinli"):Text(L"Block",L"Engelli")):
          setting==CEF_CONTENT_SETTING_VALUE_ALLOW?Text(L"Allow (engine)",L"İzinli (motor)"):
          setting==CEF_CONTENT_SETTING_VALUE_BLOCK?Text(L"Block (engine)",L"Engelli (motor)"):Text(L"Ask",L"Sor");
      auto line=Wide(rule.origin)+L"    •    "+PermissionName(rule.kind)+L"    •    "+value;
      if(rule.top!=rule.origin)line+=L"    •    "+Wide(rule.top);
      SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(line.c_str()));
    }
    if(previous_permission) {
      selected=-1;
      for(size_t i=0;i<data->permission_rows.size();++i) {
        const auto& row=data->permission_rows[i];
        if(row.origin==previous_permission->origin&&row.top==previous_permission->top&&row.kind==previous_permission->kind)selected=static_cast<int>(i);
      }
    }
    bool enabled=selected>=0&&selected<static_cast<int>(data->permission_rows.size());
    for(int id:{32,33,34})EnableWindow(GetDlgItem(panel,id),enabled);
  }
  SendMessageW(list,LB_SETCURSEL,selected,0);
  if(kind==Downloads) {
    Download* download=nullptr;
    if(selected>=0&&selected<static_cast<int>(downloads_.size())){auto item=downloads_.begin();std::advance(item,selected);download=&item->second;}
    EnableWindow(GetDlgItem(panel,31),download&&!download->path.empty());
    EnableWindow(GetDlgItem(panel,32),download&&download->callback&&!download->complete&&!download->canceled);
    EnableWindow(GetDlgItem(panel,33),download&&download->callback&&!download->complete&&!download->canceled);
  }
}
void Window::SettingsApplied(HWND panel) {
  auto checked=[&](int id){return SendMessageW(GetDlgItem(panel,id),BM_GETCHECK,0,0)==BST_CHECKED;};
  store_.settings.shield=checked(40);store_.settings.third_party_cookies=checked(41);store_.settings.https_only=checked(42);store_.settings.doh=checked(43);store_.settings.dark=checked(44);store_.settings.turkish=checked(45);
  store_.settings.restore_session=checked(46);
  shield->enabled=store_.settings.shield;shield->block_cookies=store_.settings.third_party_cookies;shield->https_only=store_.settings.https_only;
  DeleteObject(background_);background_=CreateSolidBrush(store_.settings.dark?RGB(16,25,35):RGB(245,247,250));Save();Refresh();
  MessageBoxW(panel,Text(L"Saved. Reload pages for Shield changes. Restart for DNS/locale changes.",L"Kaydedildi. Kalkan değişiklikleri için sayfaları yenileyin. DNS/dil için yeniden başlatın.").c_str(),L"Turan Browser",MB_OK);
}
LRESULT CALLBACK Window::PanelProc(HWND h,UINT msg,WPARAM w,LPARAM l) {
  auto data=reinterpret_cast<PanelData*>(GetWindowLongPtrW(h,GWLP_USERDATA));
  if(msg==WM_NCCREATE){data=static_cast<PanelData*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(data));}
  if(!data)return DefWindowProcW(h,msg,w,l);auto self=data->owner;int kind=data->kind;
  if(msg==WM_CREATE) {
    if(kind==FindPage) {
      auto tab=self->Active();data->browser_id=tab?tab->id:-1;
      Control(h,L"EDIT",L"",60,20,22,480,30,ES_AUTOHSCROLL|WS_BORDER);
      Control(h,L"BUTTON",self->Text(L"Next",L"Sonraki").c_str(),61,20,70,120,32);
      Control(h,L"BUTTON",self->Text(L"Previous",L"Önceki").c_str(),62,154,70,120,32);
      Control(h,L"BUTTON",self->Text(L"Match case",L"Büyük/küçük harf").c_str(),64,296,70,210,32,BS_AUTOCHECKBOX);
      Control(h,L"STATIC",L"0 / 0",65,20,116,500,32);
      SetFocus(GetDlgItem(h,60));
    } else if(kind==ShieldPage) {
      Control(h,L"STATIC",L"",80,16,12,694,74);
      Control(h,L"LISTBOX",L"",30,16,92,694,242,WS_VSCROLL|WS_HSCROLL|WS_BORDER);
      Control(h,L"BUTTON",L"",81,16,348,216,32);
      Control(h,L"BUTTON",self->Text(L"Edit custom rules",L"Özel kuralları düzenle").c_str(),82,246,348,214,32);
      Control(h,L"BUTTON",self->Text(L"Privacy settings",L"Gizlilik ayarları").c_str(),83,476,348,220,32);
      Control(h,L"STATIC",self->Text(L"Records stay in memory and reset on navigation/close. Private site exceptions stay in the private tab. Network-cookie protection is separate from Shield site exceptions.",L"Kayıtlar bellekte kalır; gezinme/kapanışta sıfırlanır. Gizli site istisnaları gizli sekmede kalır. Ağ çerez koruması Kalkan site istisnalarından ayrıdır.").c_str(),0,16,394,694,48);
      self->RefreshPanel(h,kind);SetTimer(h,1,750,nullptr);
    } else if(kind==CustomFilters) {
      Control(h,L"STATIC",self->Text(L"ABP network rules and CSS selectors; unsupported options reject the save. 64 KiB / 2000 lines. Reload existing pages after applying.",L"ABP ağ kuralları ve CSS seçiciler; desteklenmeyen seçenek kaydı reddeder. 64 KiB / 2000 satır. Uygulamadan sonra açık sayfaları yenileyin.").c_str(),0,16,12,694,48);
      auto edit=Control(h,L"EDIT",Wide(self->shield->CustomRules()).c_str(),84,16,66,694,268,ES_MULTILINE|ES_WANTRETURN|WS_VSCROLL|WS_HSCROLL|ES_AUTOHSCROLL|ES_AUTOVSCROLL|WS_BORDER);
      SendMessageW(edit,EM_SETLIMITTEXT,64*1024,0);
      Control(h,L"BUTTON",self->Text(L"Validate & apply",L"Doğrula ve uygula").c_str(),85,16,350,214,32);
      Control(h,L"STATIC",L"",86,16,394,694,48);SetTimer(h,1,100,nullptr);
    } else if(kind==TorBrowserPage) {
      auto tab=self->Active();data->tor_url=tab?tab->url:std::string();data->tor_private=tab&&tab->private_tab;
      auto candidates=TorBrowserCandidates(self->store_.root);auto path=candidates.empty()?std::filesystem::path():candidates.front();
      Control(h,L"STATIC",self->Text(L"External Tor Browser folder (contains Browser\\firefox.exe)",L"Harici Tor Browser klasörü (Browser\\firefox.exe içerir)").c_str(),0,16,12,694,24);
      auto edit=Control(h,L"EDIT",path.c_str(),90,16,42,554,30,ES_AUTOHSCROLL|WS_BORDER);SendMessageW(edit,EM_SETLIMITTEXT,8192,0);
      Control(h,L"BUTTON",self->Text(L"Browse…",L"Gözat…").c_str(),91,582,42,128,30);
      Control(h,L"STATIC",self->Text(L"Only this page URL will be sent to the external browser:",L"Harici tarayıcıya yalnızca bu sayfanın adresi gönderilecek:").c_str(),0,16,86,694,24);
      Control(h,L"EDIT",Wide(data->tor_url).c_str(),92,16,116,694,60,ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_BORDER);
      Control(h,L"STATIC",self->Text(L"Turan does not copy cookies, passwords or profile data, or change Tor Browser settings. Sending a URL can disclose tokens in its query. This does not put Turan browsing on Tor.",L"Turan çerez, parola veya profil verisi kopyalamaz; Tor Browser ayarlarını değiştirmez. Adres gönderimi sorgusundaki token'ları açığa çıkarabilir. Bu işlem Turan gezinmesini Tor ağına yönlendirmez.").c_str(),0,16,188,694,66);
      Control(h,L"STATIC",data->tor_private?self->Text(L"Private page: the URL is sent only when you click Open; the folder choice is not saved.",L"Gizli sayfa: adres yalnız Aç'a bastığınızda gönderilir; klasör seçimi kaydedilmez.").c_str():self->Text(L"Verified catalog: Tor Browser 15.0.24. Other/updated versions require catalog renewal.",L"Doğrulama kataloğu: Tor Browser 15.0.24. Diğer/güncellenen sürümler katalog yenilemesi gerektirir.").c_str(),0,16,266,694,42);
      Control(h,L"BUTTON",self->Text(L"Verify installation",L"Kurulumu doğrula").c_str(),93,16,320,216,34);
      Control(h,L"BUTTON",self->Text(L"Open in Tor Browser",L"Tor Browser ile aç").c_str(),94,246,320,254,34);
      std::wstring target;std::string error;bool eligible=TorTargetUrl(data->tor_url,target,error);
      EnableWindow(GetDlgItem(h,94),eligible);
      Control(h,L"STATIC",eligible?self->Text(L"Choose your official installation, then verify or open.",L"Resmî kurulumunuzu seçip doğrulayın veya açın.").c_str():self->Text(L"This address cannot be shared: only HTTP(S) pages without credentials are accepted.",L"Bu adres gönderilemez: yalnız kimlik bilgisi içermeyen HTTP(S) sayfalar kabul edilir.").c_str(),95,16,372,694,68);
      SetTimer(h,1,100,nullptr);
    } else if(kind==SettingsPage) {
      const wchar_t* en[]={L"Turan Shield • Network and cosmetic filters",L"Block third-party network cookies",L"HTTPS-only (localhost exempt)",L"Secure DNS • Cloudflare DoH (restart required)",L"Dark native toolbar",L"Türkçe / Turkish (restart engine locale)"};
      const wchar_t* tr[]={L"Turan Kalkanı • Başlangıç reklam/takipçi listesini engelle",L"Üçüncü taraf ağ çerezlerini engelle",L"Yalnız HTTPS (localhost hariç)",L"Güvenli DNS • Cloudflare DoH (yeniden başlatın)",L"Koyu araç çubuğu",L"Türkçe / Turkish (motor dilini yeniden başlatın)"};
      bool values[]={self->store_.settings.shield,self->store_.settings.third_party_cookies,self->store_.settings.https_only,self->store_.settings.doh,self->store_.settings.dark,self->store_.settings.turkish};
      for(int i=0;i<6;++i){auto box=Control(h,L"BUTTON",self->Text(en[i],tr[i]).c_str(),40+i,20,22+i*40,670,28,BS_AUTOCHECKBOX);SendMessageW(box,BM_SETCHECK,values[i]?BST_CHECKED:BST_UNCHECKED,0);}
      auto restore=Control(h,L"BUTTON",self->Text(L"Restore previous tabs at startup",L"Başlangıçta önceki sekmeleri aç").c_str(),46,20,265,670,28,BS_AUTOCHECKBOX);
      SendMessageW(restore,BM_SETCHECK,self->store_.settings.restore_session?BST_CHECKED:BST_UNCHECKED,0);
      Control(h,L"STATIC",self->Text(L"Site permissions can be allowed, blocked or reset per profile.\nShield supports starter, custom and optional validated filter subscriptions.",L"Site izinleri profil bazında verilebilir, engellenebilir veya sıfırlanabilir.\nKalkan başlangıç, özel ve isteğe bağlı doğrulanmış filtre aboneliklerini destekler.").c_str(),0,20,305,680,50);
      Control(h,L"BUTTON",self->Text(L"Save settings",L"Ayarları kaydet").c_str(),50,20,364,160,34);
      Control(h,L"BUTTON",self->Text(L"Clear cookies",L"Çerezleri temizle").c_str(),51,194,364,160,34);
    } else {
      Control(h,L"LISTBOX",L"",30,16,16,694,360,LBS_NOTIFY|WS_VSCROLL|WS_HSCROLL|WS_BORDER);
      Control(h,L"BUTTON",kind==Downloads?self->Text(L"Show folder",L"Klasörü aç").c_str():kind==Permissions?self->Text(L"Request log",L"İstek kayıtları").c_str():kind==PermissionLog?self->Text(L"Clear log",L"Kayıtları temizle").c_str():self->Text(L"Open in new tab",L"Yeni sekmede aç").c_str(),31,16,392,160,34);
      if(kind==Downloads){Control(h,L"BUTTON",L"Pause / Resume",32,210,392,160,34);Control(h,L"BUTTON",L"Cancel",33,382,392,120,34);}
      else if(kind==Permissions) {
        Control(h,L"BUTTON",self->Text(L"Ask",L"Sor").c_str(),32,188,392,130,34);
        Control(h,L"BUTTON",self->Text(L"Allow",L"İzin ver").c_str(),33,330,392,130,34);
        Control(h,L"BUTTON",self->Text(L"Block",L"Engelle").c_str(),34,472,392,130,34);
      } else if(kind!=PermissionLog)Control(h,L"BUTTON",L"Remove selected",32,210,392,180,34);
      self->RefreshPanel(h,kind);SetTimer(h,1,750,nullptr);
    }
    for(HWND child=GetWindow(h,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(self->font_),TRUE);
    return 0;
  }
  if(msg==WM_TIMER){
    if(kind==TorBrowserPage&&data->tor_job.valid()&&data->tor_job.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
      auto result=data->tor_job.get();auto message=result.ok?self->Text(L"Verified official Tor Browser 15.0.24: ",L"Resmî Tor Browser 15.0.24 doğrulandı: ")+std::to_wstring(result.verified_files)+self->Text(L" files.",L" dosya."):Wide(result.error);
      if(result.ok&&data->tor_launch) {
        message+=self->Text(L" Started; process ID: ",L" Başlatıldı; işlem kimliği: ")+std::to_wstring(result.process_id)+self->Text(L". Tor Browser handles its own connection and profile.",L". Bağlantıyı ve kendi profilini Tor Browser yönetir.");
        if(!data->tor_private&&!SaveTorBrowserPreference(self->store_.root,data->tor_root))message+=self->Text(L" Folder preference could not be saved.",L" Klasör tercihi kaydedilemedi.");
      }
      SetWindowTextW(GetDlgItem(h,95),message.c_str());
      for(int id:{90,91,93})EnableWindow(GetDlgItem(h,id),true);
      std::wstring target;std::string error;EnableWindow(GetDlgItem(h,94),TorTargetUrl(data->tor_url,target,error));
    } else if(kind==CustomFilters&&data->filter_job.valid()&&data->filter_job.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
      auto result=data->filter_job.get();
      SetWindowTextW(GetDlgItem(h,86),result.first?self->Text(L"Saved. Reload open pages.",L"Kaydedildi. Açık sayfaları yenileyin.").c_str():Wide(result.second).c_str());
      EnableWindow(GetDlgItem(h,85),true);
    }else self->RefreshPanel(h,kind);return 0;
  }
  if(msg==WM_COMMAND) {
    int id=LOWORD(w);
    if(kind==TorBrowserPage) {
      if(id==91&&!data->tor_job.valid()) {
        wchar_t filename[32768]={};OPENFILENAMEW choice={sizeof(choice)};choice.hwndOwner=h;
        choice.lpstrFilter=L"Tor Browser (firefox.exe)\0firefox.exe\0\0";choice.lpstrFile=filename;choice.nMaxFile=32768;
        auto title=self->Text(L"Select Tor Browser's Browser\\firefox.exe",L"Tor Browser'ın Browser\\firefox.exe dosyasını seçin");choice.lpstrTitle=title.c_str();
        choice.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
        if(GetOpenFileNameW(&choice)) {auto root=std::filesystem::path(filename).parent_path().parent_path();SetWindowTextW(GetDlgItem(h,90),root.c_str());}
      }
      if((id==93||id==94)&&!data->tor_job.valid()) {
        data->tor_root=std::filesystem::path(GetText(GetDlgItem(h,90)));data->tor_launch=id==94;
        auto root=data->tor_root;auto url=data->tor_url;bool launch=data->tor_launch;
        for(int control:{90,91,93,94})EnableWindow(GetDlgItem(h,control),false);
        SetWindowTextW(GetDlgItem(h,95),self->Text(L"Checking authenticated release files…",L"İmzayla doğrulanan sürüm dosyaları denetleniyor…").c_str());
        data->tor_job=std::async(std::launch::async,[root,url,launch](){return launch?OpenInTorBrowser(root,url):VerifyTorBrowser(root);});
      }return 0;
    }
    if(kind==CustomFilters) {
      if(id==85&&!data->filter_job.valid()) {
        auto rules=Utf8(GetText(GetDlgItem(h,84)));auto shield=self->shield;
        EnableWindow(GetDlgItem(h,85),false);SetWindowTextW(GetDlgItem(h,86),self->Text(L"Validating…",L"Doğrulanıyor…").c_str());
        data->filter_job=std::async(std::launch::async,[shield,rules](){std::string error;bool ok=shield->SaveCustom(rules,error);return std::make_pair(ok,error);});
      }return 0;
    }
    if(kind==ShieldPage) {
      if(id==83)self->Panel(SettingsPage);
      auto tab=std::find_if(self->tabs_.begin(),self->tabs_.end(),[&](const Tab& t){return t.browser&&t.browser->GetIdentifier()==data->browser_id;});
      if(id==82&&tab!=self->tabs_.end()&&!tab->private_tab)self->Panel(CustomFilters);
      if(id==81&&tab!=self->tabs_.end()) {
        std::string error;bool enabled=self->shield->SiteEnabled(data->browser_id,tab->url);
        if(self->shield->SetSiteEnabled(data->browser_id,tab->url,!enabled,error)){tab->browser->Reload();self->RefreshPanel(h,kind);}
        else SetWindowTextW(GetDlgItem(h,80),Wide(error).c_str());
      }return 0;
    }
    if(kind==FindPage) {
      auto tab=self->Find(data->browser_id);
      if(tab&&tab->browser&&(id==61||id==62||id==64||(id==60&&HIWORD(w)==EN_CHANGE))) {
        auto query=GetText(GetDlgItem(h,60));bool next=query==data->query&&(id==61||id==62);data->query=query;
        if(query.empty()){tab->browser->GetHost()->StopFinding(true);SetWindowTextW(GetDlgItem(h,65),L"0 / 0");}
        else tab->browser->GetHost()->Find(query,id!=62,SendMessageW(GetDlgItem(h,64),BM_GETCHECK,0,0)==BST_CHECKED,next);
      }return 0;
    }
    if(kind==SettingsPage) {if(id==50)self->SettingsApplied(h);if(id==51){auto t=self->Active();if(t&&t->browser)t->browser->GetHost()->GetRequestContext()->GetCookieManager(nullptr)->DeleteCookies("","",nullptr);}return 0;}
    int index=static_cast<int>(SendMessageW(GetDlgItem(h,30),LB_GETCURSEL,0,0));
    if(kind==Bookmarks||kind==History) {auto& entries=kind==Bookmarks?self->store_.bookmarks:self->store_.history;
      if(index>=0&&index<static_cast<int>(entries.size())) {if(id==31||(id==30&&HIWORD(w)==LBN_DBLCLK))self->NewTab(false,entries[index].url);if(id==32){entries.erase(entries.begin()+index);self->Save();self->RefreshPanel(h,kind);}}}
    if(kind==PermissionLog&&id==31){self->permissions_.clear();self->RefreshPanel(h,kind);}
    if(kind==Permissions) {
      if(id==31){self->Panel(PermissionLog);return 0;}
      if(id==30&&HIWORD(w)==LBN_SELCHANGE){self->RefreshPanel(h,kind);return 0;}
      if(index>=0&&index<static_cast<int>(data->permission_rows.size())&&(id==32||id==33||id==34)) {
        auto rule=data->permission_rows[index];
        if(PermissionOrigin(rule.origin)!=rule.origin||PermissionOrigin(rule.top)!=rule.top)return 0;
        if(id==32) {
          auto& rules=self->store_.permissions;
          rules.erase(std::remove_if(rules.begin(),rules.end(),[&](const PermissionRule& item){return item.origin==rule.origin&&item.top==rule.top&&item.kind==rule.kind;}),rules.end());
        } else if(!RememberPermission(self->store_.permissions,rule.origin,rule.top,rule.kind,id==33))return 0;
        self->normal_->SetContentSetting(rule.origin,rule.top,EnginePermissionType(rule.kind),id==32?CEF_CONTENT_SETTING_VALUE_DEFAULT:id==33?CEF_CONTENT_SETTING_VALUE_ALLOW:CEF_CONTENT_SETTING_VALUE_BLOCK);
        self->Save();self->RefreshPanel(h,kind);
      }return 0;
    }
    if(kind==Downloads&&id==30&&HIWORD(w)==LBN_SELCHANGE){self->RefreshPanel(h,kind);return 0;}
    if(kind==Downloads&&index>=0&&index<static_cast<int>(self->downloads_.size())) {auto it=self->downloads_.begin();std::advance(it,index);auto& d=it->second;
      if(id==31&&!d.path.empty()) {auto folder=std::filesystem::path(Wide(d.path)).parent_path();ShellExecuteW(h,L"open",folder.c_str(),nullptr,nullptr,SW_SHOW);}
      if(id==32&&d.callback&&!d.complete&&!d.canceled){if(d.paused)d.callback->Resume();else d.callback->Pause();d.paused=!d.paused;}
      if(id==33&&d.callback&&!d.complete&&!d.canceled)d.callback->Cancel();}
    return 0;
  }
  if(msg==WM_NCDESTROY){if(kind==FindPage){auto tab=self->Find(data->browser_id);if(tab&&tab->browser)tab->browser->GetHost()->StopFinding(true);}self->panels_.erase(std::remove(self->panels_.begin(),self->panels_.end(),h),self->panels_.end());delete data;SetWindowLongPtrW(h,GWLP_USERDATA,0);}
  return DefWindowProcW(h,msg,w,l);
}
LRESULT CALLBACK Window::EditProc(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR pointer) {
  auto self=reinterpret_cast<Window*>(pointer);
  if(msg==WM_KEYDOWN){if(w==VK_RETURN){self->NavigateBar();return 0;}if(self->Shortcut(static_cast<int>(w),(GetKeyState(VK_CONTROL)&0x8000)!=0,(GetKeyState(VK_SHIFT)&0x8000)!=0))return 0;}
  return DefSubclassProc(h,msg,w,l);
}
LRESULT CALLBACK Window::Proc(HWND h,UINT msg,WPARAM w,LPARAM l) {
  auto self=reinterpret_cast<Window*>(GetWindowLongPtrW(h,GWLP_USERDATA));
  if(msg==WM_NCCREATE){self=static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->hwnd_=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
  return self?self->Message(msg,w,l):DefWindowProcW(h,msg,w,l);
}
LRESULT CALLBACK Window::TabProc(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR pointer) {
  auto self=reinterpret_cast<Window*>(pointer);
  if(msg==WM_LBUTTONDOWN){TCHITTESTINFO hit={};hit.pt={static_cast<short>(LOWORD(l)),static_cast<short>(HIWORD(l))};self->drag_tab_=TabCtrl_HitTest(h,&hit);self->drag_start_=hit.pt;SetCapture(h);}
  if(msg==WM_LBUTTONUP){ReleaseCapture();TCHITTESTINFO hit={};hit.pt={static_cast<short>(LOWORD(l)),static_cast<short>(HIWORD(l))};int to=TabCtrl_HitTest(h,&hit);if(abs(hit.pt.x-self->drag_start_.x)>8)self->MoveTab(self->drag_tab_,to);self->drag_tab_=-1;}
  if(msg==WM_CONTEXTMENU){POINT point={static_cast<short>(LOWORD(l)),static_cast<short>(HIWORD(l))};if(point.x==-1){GetCursorPos(&point);}POINT local=point;ScreenToClient(h,&local);TCHITTESTINFO hit={};hit.pt=local;int index=TabCtrl_HitTest(h,&hit);if(index>=0)self->Select(index);auto t=self->Active();
    HMENU menu=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING,Pin,self->Text(t&&t->pinned?L"Unpin tab":L"Pin tab",t&&t->pinned?L"Sabitlemeyi kaldır":L"Sekmeyi sabitle").c_str());
    AppendMenuW(menu,MF_STRING,Mute,self->Text(t&&t->muted?L"Unmute tab":L"Mute tab",t&&t->muted?L"Sekmenin sesini aç":L"Sekmeyi sessize al").c_str());
    AppendMenuW(menu,MF_STRING,GroupWork,self->Text(L"Group: Work (blue)",L"Grup: İş (mavi)").c_str());
    AppendMenuW(menu,MF_STRING,GroupPersonal,self->Text(L"Group: Personal (green)",L"Grup: Kişisel (yeşil)").c_str());
    AppendMenuW(menu,MF_STRING,GroupResearch,self->Text(L"Group: Research (purple)",L"Grup: Araştırma (mor)").c_str());
    AppendMenuW(menu,MF_STRING,Ungroup,self->Text(L"Remove from group",L"Gruptan çıkar").c_str());
    AppendMenuW(menu,MF_STRING,CloseTab,self->Text(L"Close tab",L"Sekmeyi kapat").c_str());
    int chosen=TrackPopupMenu(menu,TPM_RETURNCMD,point.x,point.y,0,self->hwnd_,nullptr);DestroyMenu(menu);if(chosen)self->Command(chosen);return 0;
  }
  if(msg==WM_ERASEBKGND)return 1;
  if(msg==WM_PAINT) {
    PAINTSTRUCT paint;HDC dc=BeginPaint(h,&paint);RECT area;GetClientRect(h,&area);FillRect(dc,&area,self->background_);
    SelectObject(dc,self->font_);SetBkMode(dc,TRANSPARENT);
    for(int i=0;i<static_cast<int>(self->tabs_.size());++i) {
      RECT r;if(!TabCtrl_GetItemRect(h,i,&r))continue;
      bool selected=i==self->active_;COLORREF bg=self->store_.settings.dark?(selected?RGB(46,59,73):RGB(27,39,51)):(selected?RGB(210,220,232):RGB(225,232,240));
      auto brush=CreateSolidBrush(bg);FillRect(dc,&r,brush);DeleteObject(brush);
      if(selected){RECT line=r;line.top=line.bottom-2;auto gold=CreateSolidBrush(RGB(220,175,88));FillRect(dc,&line,gold);DeleteObject(gold);}
      SetTextColor(dc,self->store_.settings.dark?RGB(232,237,241):RGB(20,30,40));InflateRect(&r,-10,-2);
      auto& t=self->tabs_[i];
      if(t.color){const COLORREF colors[]={0,RGB(85,150,230),RGB(70,175,110),RGB(160,105,215)};RECT stripe=r;stripe.right=stripe.left+3;auto color=CreateSolidBrush(colors[std::min(t.color,3u)]);FillRect(dc,&stripe,color);DeleteObject(color);}
      auto label=(t.pinned?L"◆ ":L"")+std::wstring(t.muted?L"♪̸ ":L"")+(t.private_tab?L"◈ ":L"")+Wide(t.title);DrawTextW(dc,label.c_str(),-1,&r,DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    }
    EndPaint(h,&paint);return 0;
  }
  return DefSubclassProc(h,msg,w,l);
}
LRESULT Window::Message(UINT msg,WPARAM w,LPARAM l) {
  switch(msg) {
    case WM_ACTIVATE:
      if(reparent_chrome_&&LOWORD(w)!=WA_INACTIVE)PostMessageW(hwnd_,WM_APP+7,0,0);
      break;
    case WM_APP+7:
      if(reparent_chrome_&&!closing_)if(auto tab=Active();tab&&tab->browser)
        NativeChromeHost::ActivateWithinOwner(tab->browser->GetHost()->GetWindowHandle(),hwnd_);
      return 0;
    case WM_APP+6:
      if((reparent_chrome_||integrated_chrome_)&&!closing_)for(size_t i=0;i<tabs_.size();++i)if(tabs_[i].id==static_cast<int>(w)) {
        active_=static_cast<int>(i);TabCtrl_SetCurSel(tabs_control_,active_);Refresh();SaveSession();break;
      }return 0;
    case WM_APP+5:SetWindowTextW(status_,Text(w?L"PDF saved":L"PDF could not be saved",w?L"PDF kaydedildi":L"PDF kaydedilemedi").c_str());return 0;
    case WM_APP+3:{auto tab=Find(static_cast<int>(w));if(tab&&tab->browser&&!closing_)tab->browser->GetMainFrame()->LoadURL(tab->url);return 0;}
    case WM_APP+2:
      if(closing_&&tabs_.empty()) {
        auto panels=panels_;for(HWND panel:panels)if(IsWindow(panel))DestroyWindow(panel);
        for(HWND panel:credential_windows_)if(IsWindow(panel)) {
          wchar_t name[64]={};GetClassNameW(panel,name,64);
          if(std::wstring(name)==L"TuranCredentialWindow")DestroyWindow(panel);
        }
        credential_windows_.clear();engine_windows_.reset();
        for(HWND panel:subscription_windows_)if(IsWindow(panel)) {
          wchar_t name[64]={};GetClassNameW(panel,name,64);
          if(std::wstring(name)==L"TuranFilterSubscriptionsPanel")DestroyWindow(panel);
        }
        subscription_windows_.clear();
        for(HWND panel:update_windows_)if(IsWindow(panel)) {
          wchar_t name[64]={};GetClassNameW(panel,name,64);
          if(std::wstring(name)==L"TuranSecureUpdateWindow")DestroyWindow(panel);
        }
        update_windows_.clear();
        if(engine_icon_){DestroyIcon(engine_icon_);engine_icon_=nullptr;}
        panels_.clear();DestroyWindow(hwnd_);hwnd_=nullptr;CefQuitMessageLoop();
      }return 0;
    case WM_APP+1:if(IsWindow(reinterpret_cast<HWND>(w)))DestroyWindow(reinterpret_cast<HWND>(w));return 0;
    case WM_SIZE:Layout();return 0;
    case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={780,480};return 0;
    case WM_COMMAND:Command(LOWORD(w));return 0;
    case WM_NOTIFY:if(reinterpret_cast<NMHDR*>(l)->hwndFrom==tabs_control_&&reinterpret_cast<NMHDR*>(l)->code==TCN_SELCHANGE)Select(TabCtrl_GetCurSel(tabs_control_));return 0;
    case WM_CLOSE:Close();return 0;
    case WM_TIMER:
      if(w==1)PollSubscriptions();
      if(w==2&&smoke_pass_) {std::ofstream out(smoke_result_);out<<"CEF_RENDER_JS_OK\nSANDBOX_BOOTSTRAP_OK\nPRIVATE_COOKIE_ISOLATION_OK\nNORMAL_COOKIE_CONTEXT_OK\nDOWNLOAD_OK\nSHIELD_REQUEST_BLOCK_OK\nTABS="<<tabs_.size()<<"\n";out.close();Close();}
      else if(w==1){if(is_smoke()) {static int ticks=0;if(++ticks>60){Close();}}else {auto tab=Active();auto events=tab&&tab->browser?shield->Events(tab->browser->GetIdentifier()):Shield::Snapshot{};if(displayed_blocked_!=events.ads+events.trackers+events.custom)Refresh();}}return 0;
    case WM_KEYDOWN:if(Shortcut(static_cast<int>(w),(GetKeyState(VK_CONTROL)&0x8000)!=0,(GetKeyState(VK_SHIFT)&0x8000)!=0))return 0;break;
    case WM_ERASEBKGND:{RECT r;GetClientRect(hwnd_,&r);FillRect(reinterpret_cast<HDC>(w),&r,background_);return 1;}
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:{HDC dc=reinterpret_cast<HDC>(w);SetTextColor(dc,store_.settings.dark?RGB(232,237,241):RGB(20,30,40));SetBkColor(dc,store_.settings.dark?RGB(16,25,35):RGB(245,247,250));return reinterpret_cast<LRESULT>(background_);}
    case WM_DRAWITEM:{auto draw=reinterpret_cast<DRAWITEMSTRUCT*>(l);COLORREF bg=store_.settings.dark?RGB(27,39,51):RGB(225,232,240);bool selected=false;
      std::wstring label;
      if(draw->CtlID==10&&draw->itemID<tabs_.size()){selected=draw->itemID==static_cast<UINT>(active_);auto& t=tabs_[draw->itemID];label=(t.private_tab?L"◈ ":L"")+Wide(t.title);}else label=GetText(draw->hwndItem);
      if(selected)bg=store_.settings.dark?RGB(46,59,73):RGB(210,220,232);auto brush=CreateSolidBrush(bg);FillRect(draw->hDC,&draw->rcItem,brush);DeleteObject(brush);
      SetBkMode(draw->hDC,TRANSPARENT);SetTextColor(draw->hDC,(draw->itemState&ODS_DISABLED)?RGB(100,110,120):store_.settings.dark?RGB(224,183,96):RGB(35,57,80));SelectObject(draw->hDC,font_);RECT text=draw->rcItem;InflateRect(&text,-8,-2);DrawTextW(draw->hDC,label.c_str(),-1,&text,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);return TRUE;}
    case WM_PAINT:{PAINTSTRUCT paint;HDC dc=BeginPaint(hwnd_,&paint);SelectObject(dc,font_);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(224,183,96));TextOutW(dc,20,22,L"◆ TURAN",7);EndPaint(hwnd_,&paint);return 0;}
    case WM_DESTROY:DeleteObject(background_);DeleteObject(font_);return 0;
  }
  return DefWindowProcW(hwnd_,msg,w,l);
}
}
