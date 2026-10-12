#pragma once
#include "include/cef_client.h"
#include "include/cef_display_handler.h"
#include "include/cef_download_handler.h"
#include "include/cef_keyboard_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_permission_handler.h"
#include "include/cef_request_handler.h"
#include "include/cef_find_handler.h"
#include "include/cef_dialog_handler.h"
#include "include/cef_focus_handler.h"
#include "ui/window.h"

namespace turan {
class Client final : public CefClient, public CefLifeSpanHandler, public CefDisplayHandler,
  public CefLoadHandler, public CefRequestHandler, public CefDownloadHandler,
  public CefPermissionHandler, public CefKeyboardHandler, public CefFindHandler,public CefDialogHandler,public CefFocusHandler {
 public:
  Client(Window* owner,int id,bool private_tab):owner_(owner),id_(id),private_tab_(private_tab),shield_(owner->shield){}
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {return this;}
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override {return this;}
  CefRefPtr<CefLoadHandler> GetLoadHandler() override {return this;}
  CefRefPtr<CefRequestHandler> GetRequestHandler() override {return this;}
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override {return this;}
  CefRefPtr<CefPermissionHandler> GetPermissionHandler() override {return this;}
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override {return this;}
  CefRefPtr<CefFindHandler> GetFindHandler() override {return this;}
  CefRefPtr<CefDialogHandler> GetDialogHandler() override {return this;}
  CefRefPtr<CefFocusHandler> GetFocusHandler() override {return this;}
  void OnGotFocus(CefRefPtr<CefBrowser> browser) override {owner_->EngineActivated(browser);}
  bool OnFileDialog(CefRefPtr<CefBrowser>,FileDialogMode,const CefString&,const CefString&,const std::vector<CefString>&,const std::vector<CefString>&,const std::vector<CefString>&,CefRefPtr<CefFileDialogCallback>) override;
  void OnFindResult(CefRefPtr<CefBrowser>,int,int,const CefRect&,int,bool) override;
  void OnFullscreenModeChange(CefRefPtr<CefBrowser>,bool) override;
  void OnAfterCreated(CefRefPtr<CefBrowser>) override;
  void OnBeforeClose(CefRefPtr<CefBrowser>) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override {
    PostMessageW(owner_->handle(),WM_APP+1,reinterpret_cast<WPARAM>(browser->GetHost()->GetWindowHandle()),0);
    return true;
  }
  bool OnBeforePopup(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,int,const CefString&,const CefString&,
    cef_window_open_disposition_t,bool,const CefPopupFeatures&,CefWindowInfo&,CefRefPtr<CefClient>&,
    CefBrowserSettings&,CefRefPtr<CefDictionaryValue>&,bool*) override;
  bool OnOpenURLFromTab(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,const CefString&,cef_window_open_disposition_t,bool) override;
  void OnAddressChange(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,const CefString&) override;
  void OnTitleChange(CefRefPtr<CefBrowser>,const CefString&) override;
  void OnLoadingStateChange(CefRefPtr<CefBrowser>,bool,bool,bool) override;
  void OnLoadEnd(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,int) override;
  void OnLoadError(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,ErrorCode,const CefString&,const CefString&) override;
  bool OnBeforeBrowse(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,CefRefPtr<CefRequest>,bool,bool) override;
  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,CefRefPtr<CefRequest>,bool,bool,const CefString&,bool&) override;
  bool OnBeforeDownload(CefRefPtr<CefBrowser>,CefRefPtr<CefDownloadItem>,const CefString&,CefRefPtr<CefBeforeDownloadCallback>) override;
  void OnDownloadUpdated(CefRefPtr<CefBrowser>,CefRefPtr<CefDownloadItem>,CefRefPtr<CefDownloadItemCallback>) override;
  bool OnRequestMediaAccessPermission(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,const CefString&,uint32_t,CefRefPtr<CefMediaAccessCallback>) override;
  bool OnShowPermissionPrompt(CefRefPtr<CefBrowser>,uint64_t,const CefString&,uint32_t,CefRefPtr<CefPermissionPromptCallback>) override;
  void OnDismissPermissionPrompt(CefRefPtr<CefBrowser>,uint64_t,cef_permission_request_result_t) override;
  bool OnPreKeyEvent(CefRefPtr<CefBrowser>,const CefKeyEvent&,CefEventHandle,bool*) override;
 private:
  Window* owner_;int id_;bool private_tab_;
  std::shared_ptr<Shield> shield_;
  IMPLEMENT_REFCOUNTING(Client);
};
}
