#pragma once
#include "security/secret.h"
#include "include/cef_browser.h"
#include <functional>
#include <atomic>
#include <memory>
#include <string>
namespace turan {
struct CredentialForm {std::string origin,username;Secret password;};
using CredentialCaptureReply=std::function<void(CredentialForm,std::string)>;
// UI-thread only, explicit native user action. No page-to-native message bridge.
// Only the main frame, valid TLS and same-origin form action are eligible.
void CaptureCredentialForm(CefRefPtr<CefBrowser>,CredentialCaptureReply,std::shared_ptr<std::atomic_bool> cancelled={});
void FillCredentialForm(CefRefPtr<CefBrowser>,const std::string& exact_origin,const std::string& username,Secret password,std::function<void(bool,std::string)>,std::shared_ptr<std::atomic_bool> cancelled={});
bool CredentialPageEligible(CefRefPtr<CefBrowser>,std::string& origin,std::string& error);
}
