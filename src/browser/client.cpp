#include "browser/client.h"
#include "include/cef_resource_request_handler.h"
#include "include/wrapper/cef_helpers.h"
#include "include/cef_parser.h"
#include <commdlg.h>

namespace turan {
class CookieFilter final : public CefCookieAccessFilter {
 public:
  CookieFilter(std::shared_ptr<Shield> shield,std::string first):shield_(std::move(shield)),first_(std::move(first)){}
  bool CanSendCookie(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,CefRefPtr<CefRequest> r,const CefCookie&) override {return !shield_->block_cookies||!shield_->ThirdParty(r->GetURL(),first_);}
  bool CanSaveCookie(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,CefRefPtr<CefRequest> r,CefRefPtr<CefResponse>,const CefCookie&) override {return !shield_->block_cookies||!shield_->ThirdParty(r->GetURL(),first_);}
 private:
  std::shared_ptr<Shield> shield_;std::string first_;
  IMPLEMENT_REFCOUNTING(CookieFilter);
};
class ResourcePolicy final : public CefResourceRequestHandler {
 public:
  ResourcePolicy(std::shared_ptr<Shield> shield,std::string first,std::string document,int browser):shield_(std::move(shield)),first_(std::move(first)),document_(std::move(document)),browser_(browser),generation_(shield_->Generation(browser)){}
  ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,CefRefPtr<CefRequest> r,CefRefPtr<CefCallback>) override {
    FilterType type=FilterType::Other;
    switch(r->GetResourceType()) {
      case RT_MAIN_FRAME:type=FilterType::Document;break;case RT_SUB_FRAME:type=FilterType::Subdocument;break;
      case RT_SCRIPT:type=FilterType::Script;break;case RT_IMAGE:type=FilterType::Image;break;
      case RT_STYLESHEET:type=FilterType::Stylesheet;break;case RT_FONT_RESOURCE:type=FilterType::Font;break;
      case RT_MEDIA:type=FilterType::Media;break;case RT_XHR:type=FilterType::Xhr;break;
      case RT_PING:type=FilterType::Ping;break;case RT_OBJECT:type=FilterType::Object;break;default:break;
    }
    if(shield_->Check(browser_,{r->GetURL(),document_,type},generation_).blocked)return RV_CANCEL;
    if(shield_->https_only) { auto old=r->GetURL().ToString(),updated=Navigate(old,true);if(old.rfind("http://",0)==0&&updated!=old)r->SetURL(updated); }
    return RV_CONTINUE;
  }
  CefRefPtr<CefCookieAccessFilter> GetCookieAccessFilter(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,CefRefPtr<CefRequest>) override {return new CookieFilter(shield_,first_);}
 private:
  std::shared_ptr<Shield> shield_;std::string first_,document_;int browser_;uint64_t generation_;
  IMPLEMENT_REFCOUNTING(ResourcePolicy);
};
void Client::OnAfterCreated(CefRefPtr<CefBrowser> b) {CEF_REQUIRE_UI_THREAD();owner_->Created(id_,b);shield_->Register(b->GetIdentifier(),owner_->IsPrivateBrowser(b));}
void Client::OnBeforeClose(CefRefPtr<CefBrowser> b) {CEF_REQUIRE_UI_THREAD();shield_->Forget(b->GetIdentifier());owner_->CancelPermissions(b->GetIdentifier());owner_->Closed(owner_->BrowserTab(b));}
bool Client::OnBeforePopup(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame>,int,const CefString& url,const CefString&,cef_window_open_disposition_t,bool gesture,const CefPopupFeatures&,CefWindowInfo&,CefRefPtr<CefClient>&,CefBrowserSettings&,CefRefPtr<CefDictionaryValue>&,bool*) {
  if(url.ToString().rfind("chrome-extension://",0)==0&&owner_->IsInternal(url))return false;
  if(owner_->IntegratedChrome()&&gesture&&AllowedNavigation(url))return false;
  if(gesture&&AllowedNavigation(url))owner_->NewTab(owner_->IsPrivateBrowser(browser),url);return true;
}
bool Client::OnOpenURLFromTab(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame>,const CefString& url,cef_window_open_disposition_t,bool) {if(url.ToString().rfind("chrome-extension://",0)==0&&owner_->IsInternal(url))return false;if(owner_->IntegratedChrome()&&AllowedNavigation(url))return false;if(AllowedNavigation(url))owner_->NewTab(owner_->IsPrivateBrowser(browser),url);return true;}
void Client::OnAddressChange(CefRefPtr<CefBrowser> b,CefRefPtr<CefFrame> f,const CefString& url) {if(f->IsMain())owner_->Address(owner_->BrowserTab(b),url);}
void Client::OnTitleChange(CefRefPtr<CefBrowser> b,const CefString& title) {owner_->Title(owner_->BrowserTab(b),title);}
void Client::OnLoadingStateChange(CefRefPtr<CefBrowser> b,bool loading,bool,bool) {owner_->Loading(owner_->BrowserTab(b),loading);}
void Client::OnFindResult(CefRefPtr<CefBrowser> b,int,int count,const CefRect&,int ordinal,bool final) {if(final)owner_->FindResult(owner_->BrowserTab(b),count,ordinal);}
void Client::OnFullscreenModeChange(CefRefPtr<CefBrowser> b,bool fullscreen) {owner_->PageFullscreen(owner_->BrowserTab(b),fullscreen);}
bool Client::OnFileDialog(CefRefPtr<CefBrowser> b,FileDialogMode mode,const CefString&,const CefString&,const std::vector<CefString>& filters,const std::vector<CefString>&,const std::vector<CefString>&,CefRefPtr<CefFileDialogCallback> callback) {
  const auto output=owner_->TestPdfOutput();
  if(!output.empty()&&mode==FILE_DIALOG_SAVE&&Host(b->GetMainFrame()->GetURL())=="127.0.0.1"&&filters.size()==1&&filters[0]==".pdf") {
    callback->Continue({CefString(output.wstring())});return true;
  }
  return false;
}
void Client::OnLoadEnd(CefRefPtr<CefBrowser> b,CefRefPtr<CefFrame> f,int) {
  if(f->IsMain())owner_->Loaded(owner_->BrowserTab(b));
  auto url=f->GetURL().ToString();if(!AllowedNavigation(url)||url=="about:blank")return;
  auto selectors=shield_->Cosmetics(b->GetIdentifier(),url);if(selectors.empty())return;
  auto values=CefListValue::Create();for(size_t i=0;i<selectors.size();++i)values->SetString(i,selectors[i]);
  auto value=CefValue::Create();value->SetList(values);auto json=CefWriteJSON(value,JSON_WRITER_DEFAULT).ToString();
  // Filter text is data. Parse both selector and CSS rule using the engine; only
  // one style rule containing display:none!important may be published. No CSS
  // declarations, URLs, scriptlets or privileged renderer bridge are evaluated.
  std::string script="(()=>{const selectors="+json+R"JS(;
    const parsed=new CSSStyleSheet(), safe=[];
    for(const selector of selectors)try {
      document.querySelector(selector);parsed.replaceSync('');
      parsed.insertRule(selector+'{display:none!important;}');
      const r=parsed.cssRules[0];
      if(parsed.cssRules.length===1&&r instanceof CSSStyleRule&&r.style.length===1&&r.style[0]==='display'&&r.style.display==='none'&&r.style.getPropertyPriority('display')==='important')safe.push(r.cssText);
    }catch(e){}
    if(!safe.length)return;
    const style=document.createElement('style');style.dataset.turanShield='cosmetic';style.textContent=safe.join('\n');
    (document.head||document.documentElement).appendChild(style);
  })();)JS";
  f->ExecuteJavaScript(script,url,0);
}
void Client::OnLoadError(CefRefPtr<CefBrowser> b,CefRefPtr<CefFrame> f,ErrorCode code,const CefString& text,const CefString&) {if(f->IsMain()&&code!=ERR_ABORTED)owner_->Error(owner_->BrowserTab(b),text);}
bool Client::OnBeforeBrowse(CefRefPtr<CefBrowser> b,CefRefPtr<CefFrame> f,CefRefPtr<CefRequest> r,bool,bool) {
  if(f->IsMain()){shield_->Begin(b->GetIdentifier());owner_->CancelPermissions(b->GetIdentifier());}
  else owner_->CancelFramePermissions(b,f);
  auto url=r->GetURL().ToString();
  if(owner_->IntegratedChrome()&&f->IsMain()&&(url=="chrome://newtab/"||url=="chrome://new-tab-page/")) {
    f->LoadURL(owner_->StartUrl());return true;
  }
  if(!AllowedNavigation(url)&&!owner_->IsInternal(url))return true;
  if(f->IsMain()&&owner_->shield->https_only) {auto upgraded=Navigate(url,true);if(upgraded!=url){f->LoadURL(upgraded);return true;}}
  return false;
}
CefRefPtr<CefResourceRequestHandler> Client::GetResourceRequestHandler(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame> frame,CefRefPtr<CefRequest> r,bool navigation,bool,const CefString& initiator,bool&) {
  auto first=r->GetFirstPartyForCookies().ToString();if(first.empty())first=initiator;
  // initiator represents the document making the request, whereas cookie first
  // party remains the top-level site. Subframe navigation uses its initiator.
  auto document=initiator.ToString();
  if(!navigation&&frame){auto url=frame->GetURL().ToString();if(AllowedNavigation(url)&&url!="about:blank")document=url;}
  if(document.empty())document=first;
  return new ResourcePolicy(shield_,first,document,browser?browser->GetIdentifier():-1);
}
bool Client::OnBeforeDownload(CefRefPtr<CefBrowser> browser,CefRefPtr<CefDownloadItem>,const CefString& name,CefRefPtr<CefBeforeDownloadCallback> cb) {
  if(owner_->is_smoke()){cb->Continue(owner_->SmokeDownload(owner_->IsPrivateBrowser(browser)).wstring(),false);return true;}
  // Let CEF manage its asynchronous platform file chooser and modal task scope.
  cb->Continue("",true);
  return true;
}
void Client::OnDownloadUpdated(CefRefPtr<CefBrowser> b,CefRefPtr<CefDownloadItem> item,CefRefPtr<CefDownloadItemCallback> cb) {owner_->DownloadUpdated(b,item,cb);}
bool Client::OnRequestMediaAccessPermission(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame> frame,const CefString& origin,uint32_t requested,CefRefPtr<CefMediaAccessCallback> cb) {
  std::wstring what;
  uint32_t kinds=0;
  if(requested&CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE){what+=L"Microphone / Mikrofon\n";kinds|=MicrophoneAccess;}
  if(requested&CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE){what+=L"Camera / Kamera\n";kinds|=CameraAccess;}
  if(requested&(CEF_MEDIA_PERMISSION_DESKTOP_AUDIO_CAPTURE|CEF_MEDIA_PERMISSION_DESKTOP_VIDEO_CAPTURE)){what+=L"Screen capture / Ekran paylaşımı\n";kinds|=ScreenAccess;}
  const uint32_t supported=CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE|CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE|CEF_MEDIA_PERMISSION_DESKTOP_AUDIO_CAPTURE|CEF_MEDIA_PERMISSION_DESKTOP_VIDEO_CAPTURE;
  if(!requested||(requested&~supported)){cb->Continue(0);return true;}
  owner_->Permission(browser,origin,kinds,what,[cb,requested](bool allow){cb->Continue(allow?requested:0);},0,frame);return true;
}
bool Client::OnShowPermissionPrompt(CefRefPtr<CefBrowser> browser,uint64_t prompt_id,const CefString& origin,uint32_t requested,CefRefPtr<CefPermissionPromptCallback> cb) {
  std::wstring what=L"Site permission request\n";
  uint32_t kinds=0;
  if(requested&CEF_PERMISSION_TYPE_GEOLOCATION){what+=L"Location / Konum\n";kinds|=LocationAccess;}
  if(requested&CEF_PERMISSION_TYPE_NOTIFICATIONS){what+=L"Notifications / Bildirimler\n";kinds|=NotificationAccess;}
  if(requested&CEF_PERMISSION_TYPE_CLIPBOARD){what+=L"Clipboard / Pano\n";kinds|=ClipboardAccess;}
  if(requested&CEF_PERMISSION_TYPE_MIC_STREAM){what+=L"Microphone / Mikrofon\n";kinds|=MicrophoneAccess;}
  if(requested&CEF_PERMISSION_TYPE_CAMERA_STREAM){what+=L"Camera / Kamera\n";kinds|=CameraAccess;}
  const uint32_t supported=CEF_PERMISSION_TYPE_GEOLOCATION|CEF_PERMISSION_TYPE_NOTIFICATIONS|CEF_PERMISSION_TYPE_CLIPBOARD|CEF_PERMISSION_TYPE_MIC_STREAM|CEF_PERMISSION_TYPE_CAMERA_STREAM;
  if(!requested||(requested&~supported)){cb->Continue(CEF_PERMISSION_RESULT_DENY);return true;}
  owner_->Permission(browser,origin,kinds,what,[cb](bool allow){cb->Continue(allow?CEF_PERMISSION_RESULT_ACCEPT:CEF_PERMISSION_RESULT_DENY);},prompt_id);return true;
}
void Client::OnDismissPermissionPrompt(CefRefPtr<CefBrowser> browser,uint64_t prompt_id,cef_permission_request_result_t) {owner_->DismissPermission(browser->GetIdentifier(),prompt_id);}
bool Client::OnPreKeyEvent(CefRefPtr<CefBrowser>,const CefKeyEvent& e,CefEventHandle,bool*) {
  if(e.type!=KEYEVENT_RAWKEYDOWN)return false;
  return owner_->Shortcut(e.windows_key_code,(e.modifiers&EVENTFLAG_CONTROL_DOWN)!=0,(e.modifiers&EVENTFLAG_SHIFT_DOWN)!=0);
}
}
