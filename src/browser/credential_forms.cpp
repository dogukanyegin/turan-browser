#include "browser/credential_forms.h"
#include "security/vault.h"
#include "core/model.h"
#include "include/cef_parser.h"
#include "include/cef_command_line.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_task.h"
#include <windows.h>
#include <bcrypt.h>
#include <atomic>
#include <cstring>
namespace turan {
namespace {
std::atomic<int> method_sequence{1900000000};
constexpr char Function[]=R"JS(function(expected,fill,user,secret){
 'use strict';
 if(window!==window.top||!isSecureContext||location.origin!==expected||location.protocol!=='https:')return {ok:false};
 const all=Array.from(document.querySelectorAll('input[type="password"]')).filter(p=>p.isConnected&&!p.disabled&&!p.readOnly&&p.getClientRects().length&&getComputedStyle(p).visibility==='visible');
 if(all.length!==1)return {ok:false};const p=all[0];
 if(p.autocomplete==='new-password'||!p.form)return {ok:false};
 const form=p.form;let action;try{action=new URL(form.getAttribute('action')||location.href,document.baseURI);}catch(_){return {ok:false};}
 if(action.origin!==expected||action.protocol!=='https:')return {ok:false};
 const users=Array.from(form.querySelectorAll('input')).filter(i=>i.isConnected&&!i.disabled&&!i.readOnly&&i!==p&&['text','email'].includes(i.type)&&i.getClientRects().length);
 const named=users.filter(i=>i.autocomplete==='username');const u=named.length===1?named[0]:users.length===1?users[0]:null;
 if(!u)return {ok:false};
 const value=Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value');
 if(fill){if(typeof user!=='string'||typeof secret!=='string'||user.length>1024||secret.length===0||secret.length>4096)return {ok:false};
   value.set.call(u,user);value.set.call(p,secret);u.dispatchEvent(new Event('input',{bubbles:true}));u.dispatchEvent(new Event('change',{bubbles:true}));p.dispatchEvent(new Event('input',{bubbles:true}));p.dispatchEvent(new Event('change',{bubbles:true}));return {ok:true};}
 const username=value.get.call(u),password=value.get.call(p);if(username.length>1024||password.length===0||password.length>4096||username.includes('\u0000')||password.includes('\u0000'))return {ok:false};return {ok:true,username,password};
})JS";
class Operation final:public CefDevToolsMessageObserver {
 public:
 Operation(CefRefPtr<CefBrowser> browser,std::string origin,CredentialCaptureReply capture,std::function<void(bool,std::string)> fill,std::string username,Secret password,std::shared_ptr<std::atomic_bool> cancelled):cancelled_(std::move(cancelled)),browser_(browser),origin_(std::move(origin)),url_(browser->GetMainFrame()->GetURL()),capture_(std::move(capture)),fill_(std::move(fill)),username_(std::move(username)),password_(std::move(password)){}
 void Start(){
  unsigned char random[16];if(BCryptGenRandom(nullptr,random,sizeof(random),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0){Finish(false,"Secure random generation failed.");return;}
  constexpr char hex[]="0123456789abcdef";world_="TuranCredential_";for(auto c:random){world_+=hex[c>>4];world_+=hex[c&15];}
  registration_=browser_->GetHost()->AddDevToolsMessageObserver(this);if(!registration_){Finish(false,"Credential isolation could not be registered.");return;}
  Send(1,"Runtime.enable",CefDictionaryValue::Create());CefPostDelayedTask(TID_UI,new Timeout(this),10000);
 }
 void OnDevToolsMethodResult(CefRefPtr<CefBrowser>,int id,bool success,const void* result,size_t size) override {
  if(done_||id!=pending_)return;pending_=0;
  if(!Current()||!success||size>65536){Finish(false,"Page changed or credential operation was refused.");return;}
  std::string raw(static_cast<const char*>(result),size);auto parsed=CefParseJSON(raw,JSON_PARSER_RFC);SecureZeroMemory(raw.data(),raw.size());auto dict=parsed?parsed->GetDictionary():nullptr;
  if(!dict){Finish(false,"Credential operation returned invalid data.");return;}
  if(stage_==1){Send(2,"Page.getFrameTree",CefDictionaryValue::Create());return;}
  if(stage_==2){auto tree=dict->GetDictionary("frameTree");auto frame=tree?tree->GetDictionary("frame"):nullptr;if(!frame||frame->GetString("url").ToString()!=url_){Finish(false,"The document changed.");return;}frame_=frame->GetString("id");auto args=CefDictionaryValue::Create();args->SetString("frameId",frame_);args->SetString("worldName",world_);args->SetBool("grantUniveralAccess",false);Send(3,"Page.createIsolatedWorld",args);return;}
  if(stage_==3){if(unique_.empty()||context_id_!=dict->GetInt("executionContextId")){Finish(false,"The isolated context could not be verified.");return;}
   auto args=CefDictionaryValue::Create();args->SetString("functionDeclaration",Function);args->SetString("uniqueContextId",unique_);args->SetBool("returnByValue",true);args->SetBool("awaitPromise",false);args->SetBool("silent",true);
   auto values=CefListValue::Create();auto append=[&](CefRefPtr<CefValue> value){auto item=CefDictionaryValue::Create();item->SetValue("value",value);values->SetDictionary(values->GetSize(),item);};
   auto value=CefValue::Create();value->SetString(origin_);append(value);value=CefValue::Create();value->SetBool(static_cast<bool>(fill_));append(value);
   value=CefValue::Create();value->SetString(username_);append(value);value=CefValue::Create();
   int units=password_.empty()?0:MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,password_.view().data(),static_cast<int>(password_.size()),nullptr,0);
   Secret wide((static_cast<size_t>(units)+1)*sizeof(wchar_t));if(!wide.valid()||(!password_.empty()&&!units)){Finish(false,"Credential encoding failed.");return;}
   if(units)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,password_.view().data(),static_cast<int>(password_.size()),reinterpret_cast<wchar_t*>(wide.data()),units);
   value->SetString(CefString(reinterpret_cast<const wchar_t*>(wide.data()),units));wide.Clear();append(value);args->SetList("arguments",values);password_.Clear();Send(4,"Runtime.callFunctionOn",args);args->Clear();return;
  }
  if(stage_==4){auto remote=dict->GetDictionary("result");auto value=remote?remote->GetDictionary("value"):nullptr;if(dict->HasKey("exceptionDetails")||!value||!value->GetBool("ok")){Finish(false,"Use one visible username/password form with an HTTPS action on this exact site.");return;}
   if(capture_){CredentialForm form;form.origin=origin_;form.username=value->GetString("username");auto wide=value->GetString("password").ToWString();form.password=SecretFromWide(wide);SecureZeroMemory(wide.data(),wide.size()*sizeof(wchar_t));value->SetNull("password");
    if(form.username.size()>1024||!SecretUtf8Valid(form.password,4096)){Finish(false,"Credential fields exceed the supported limits.");return;}
    done_=true;auto reply=std::move(capture_);Detach();reply(std::move(form),{});
   }else Finish(true,{});
  }
 }
 void OnDevToolsEvent(CefRefPtr<CefBrowser>,const CefString& name,const void* params,size_t size) override {
  if(done_||name!="Runtime.executionContextCreated"||size>16384)return;auto parsed=CefParseJSON(std::string(static_cast<const char*>(params),size),JSON_PARSER_RFC);auto dict=parsed?parsed->GetDictionary():nullptr;auto context=dict?dict->GetDictionary("context"):nullptr;if(!context||context->GetString("name")!=world_)return;
  auto aux=context->GetDictionary("auxData");if(!aux||aux->GetString("frameId")!=frame_||aux->GetBool("isDefault"))return;unique_=context->GetString("uniqueId");context_id_=context->GetInt("id");
 }
 void OnDevToolsAgentDetached(CefRefPtr<CefBrowser>) override {Finish(false,"Page closed or renderer was replaced.");}
 private:
 bool Current(){std::string origin,error;return (!cancelled_||!cancelled_->load())&&browser_&&browser_->IsValid()&&browser_->GetMainFrame()->GetURL().ToString()==url_&&CredentialPageEligible(browser_,origin,error)&&origin==origin_;}
 void Send(int stage,const char* name,CefRefPtr<CefDictionaryValue> args){stage_=stage;pending_=++method_sequence;if(!browser_->GetHost()->ExecuteDevToolsMethod(pending_,name,args))Finish(false,"Credential method was not accepted.");}
 void Detach(){password_.Clear();registration_=nullptr;browser_=nullptr;}
 void Finish(bool ok,std::string error){if(done_)return;done_=true;auto capture=std::move(capture_);auto fill=std::move(fill_);Detach();if(capture)capture(CredentialForm{},std::move(error));else if(fill)fill(ok,std::move(error));}
 class Timeout final:public CefTask {public:explicit Timeout(CefRefPtr<Operation> operation):operation_(operation){}void Execute()override{operation_->Finish(false,"Credential operation timed out; no retry was made.");}private:CefRefPtr<Operation> operation_;IMPLEMENT_REFCOUNTING(Timeout);};
 std::shared_ptr<std::atomic_bool> cancelled_;CefRefPtr<CefBrowser> browser_;CefRefPtr<CefRegistration> registration_;std::string origin_,url_,world_,frame_,unique_,username_;int context_id_=0,pending_=0,stage_=0;bool done_=false;
 CredentialCaptureReply capture_;std::function<void(bool,std::string)> fill_;Secret password_;IMPLEMENT_REFCOUNTING(Operation);
};
}
bool CredentialPageEligible(CefRefPtr<CefBrowser> browser,std::string& origin,std::string& error){
 origin.clear();error="Password actions require a completed main-frame HTTPS page with a valid certificate and no mixed content.";
 auto command=CefCommandLine::GetGlobalCommandLine();
 if(command&&(command->HasSwitch("devtools-protocol-log-file")||command->HasSwitch("log-net-log"))){error="Password page actions are disabled while protocol/network logging is enabled.";return false;}
 if(!browser||!browser->IsValid()||browser->IsLoading())return false;auto frame=browser->GetMainFrame();if(!frame||!frame->IsValid())return false;auto url=frame->GetURL().ToString();auto candidate=Origin(url);if(!PasswordVault::OriginValid(candidate))return false;
 auto entry=browser->GetHost()->GetVisibleNavigationEntry();if(!entry||!entry->IsValid()||entry->GetURL()!=frame->GetURL()||!entry->GetCompletionTime().val)return false;
 auto ssl=entry->GetSSLStatus();if(!ssl||!ssl->IsSecureConnection()||ssl->GetCertStatus()!=CERT_STATUS_NONE||ssl->GetContentStatus()!=SSL_CONTENT_NORMAL_CONTENT)return false;
 origin=std::move(candidate);error.clear();return true;
}
void CaptureCredentialForm(CefRefPtr<CefBrowser> browser,CredentialCaptureReply reply,std::shared_ptr<std::atomic_bool> cancelled){std::string origin,error;if(!CredentialPageEligible(browser,origin,error)){reply(CredentialForm{},error);return;}CefRefPtr<Operation> operation=new Operation(browser,origin,std::move(reply),{},"",{},std::move(cancelled));operation->Start();}
void FillCredentialForm(CefRefPtr<CefBrowser> browser,const std::string& exact_origin,const std::string& username,Secret password,std::function<void(bool,std::string)> reply,std::shared_ptr<std::atomic_bool> cancelled){std::string origin,error;if(!CredentialPageEligible(browser,origin,error)||origin!=exact_origin||!SecretUtf8Valid(password,4096)||username.size()>1024){reply(false,"Credential origin, page security or data limits do not match.");return;}CefRefPtr<Operation> operation=new Operation(browser,origin,{},std::move(reply),username,std::move(password),std::move(cancelled));operation->Start();}
}
