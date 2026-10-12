#include "privacy/filter_subscription.h"
#include "core/state_file.h"
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <algorithm>
#include <chrono>
#include <sstream>

namespace turan {
namespace {
constexpr size_t MaxBody=5*1024*1024;
constexpr uint64_t Day=24*60*60,Retry=60*60;
uint64_t Now(){return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());}
bool Number(const std::string& text,uint64_t& result){if(text.empty()||text.size()>20)return false;result=0;for(char c:text){if(c<'0'||c>'9'||result>(UINT64_MAX-(c-'0'))/10)return false;result=result*10+(c-'0');}return true;}
std::string Hash(const std::string& body){
  BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;DWORD length=0,size=0;std::string result;unsigned char digest[32];
  if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
  if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&length),sizeof(length),&size,0)>=0){
    std::vector<unsigned char> object(length);
    if(BCryptCreateHash(algorithm,&hash,object.data(),length,nullptr,0,0)>=0){
      if(BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(body.data())),static_cast<ULONG>(body.size()),0)>=0&&BCryptFinishHash(hash,digest,sizeof(digest),0)>=0){const char* hex="0123456789abcdef";for(auto c:digest){result+=hex[c>>4];result+=hex[c&15];}}
      BCryptDestroyHash(hash);
    }
  }BCryptCloseAlgorithmProvider(algorithm,0);return result;
}
bool SafeProfile(const std::filesystem::path& profile){
  auto text=profile.native();if(text.size()<4||text[1]!=L':'||text[2]!=L'\\'||!profile.is_absolute()||profile.lexically_normal()!=profile)return false;
  for(auto p=profile;!p.empty();p=p.parent_path()){
    auto attributes=GetFileAttributesW(p.c_str());if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY)||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
    if(p==p.parent_path())break;
  }return true;
}
std::filesystem::path Cache(const std::filesystem::path& profile,size_t index,const wchar_t* suffix=L""){return profile/(L"shield-subscription-"+std::to_wstring(index)+L".cache"+suffix);}
std::string Pack(const std::string& text,uint64_t updated){return "TURAN-FILTER-CACHE-1\n"+std::to_string(updated)+"\n"+Hash(text)+"\n"+text;}
struct Internet {HINTERNET value=nullptr;~Internet(){if(value)WinHttpCloseHandle(value);}};
bool Header(HINTERNET request,DWORD kind,std::wstring& text){DWORD bytes=0;WinHttpQueryHeaders(request,kind,WINHTTP_HEADER_NAME_BY_INDEX,nullptr,&bytes,WINHTTP_NO_HEADER_INDEX);if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER||bytes>4096)return false;text.resize(bytes/sizeof(wchar_t));if(!WinHttpQueryHeaders(request,kind,WINHTTP_HEADER_NAME_BY_INDEX,text.data(),&bytes,WINHTTP_NO_HEADER_INDEX))return false;text.resize(bytes/sizeof(wchar_t));while(!text.empty()&&text.back()==0)text.pop_back();return true;}
}
const std::array<FilterSubscriptionDefinition,2>& FilterSubscriptions::Catalog(){
  static const std::array<FilterSubscriptionDefinition,2> catalog={{{"easylist","EasyList","https://easylist.to/easylist/easylist.txt","GPL-3.0-or-later OR CC-BY-SA-3.0-or-later",FilterCategory::Advertisement},{"easyprivacy","EasyPrivacy","https://easylist.to/easylist/easyprivacy.txt","GPL-3.0-or-later OR CC-BY-SA-3.0-or-later",FilterCategory::Tracker}}};return catalog;
}
bool FilterSubscriptions::Download(const std::string& url,std::string& body,std::string& error){
  body.clear();error.clear();auto known=std::find_if(Catalog().begin(),Catalog().end(),[&](const auto& c){return c.url==url;});if(known==Catalog().end()){error="Subscription URL is outside the official HTTPS catalog";return false;}
  auto start=std::chrono::steady_clock::now();
  // No browser cookies, credentials, referer, PAC or automatic proxy fallback.
  // Native UI explains that these optional requests use Windows TLS/direct DNS.
  Internet session{WinHttpOpen(L"Turan-Filter-Updater/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)};
  if(!session.value||!WinHttpSetTimeouts(session.value,3000,3000,5000,5000)){error="Cannot initialize subscription transport";return false;}
  DWORD protocols=WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
  protocols|=WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
  if(!WinHttpSetOption(session.value,WINHTTP_OPTION_SECURE_PROTOCOLS,&protocols,sizeof(protocols))){protocols=WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;if(!WinHttpSetOption(session.value,WINHTTP_OPTION_SECURE_PROTOCOLS,&protocols,sizeof(protocols))){error="Cannot enforce TLS 1.2 or later";return false;}}
  Internet connect{WinHttpConnect(session.value,L"easylist.to",INTERNET_DEFAULT_HTTPS_PORT,0)};
  std::wstring path(known->url.begin()+19,known->url.end());
  Internet request{connect.value?WinHttpOpenRequest(connect.value,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):nullptr};
  DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER,features=WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION,login=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
  if(!request.value||!WinHttpSetOption(request.value,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect))||!WinHttpSetOption(request.value,WINHTTP_OPTION_DISABLE_FEATURE,&features,sizeof(features))||!WinHttpSetOption(request.value,WINHTTP_OPTION_AUTOLOGON_POLICY,&login,sizeof(login))){error="Cannot enforce subscription request isolation";return false;}
  if(!WinHttpSendRequest(request.value,L"Accept: text/plain\r\nAccept-Encoding: identity\r\n",static_cast<DWORD>(-1),WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(request.value,nullptr)){error="Subscription TLS/network request failed";return false;}
  DWORD status=0,size=sizeof(status);if(!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX)||status!=200){error="Subscription server returned HTTP "+std::to_string(status)+"; redirects are refused";return false;}
  std::wstring type,encoding,length;
  if(!Header(request.value,WINHTTP_QUERY_CONTENT_TYPE,type)||type.rfind(L"text/plain",0)!=0){error="Subscription response is not plain text";return false;}
  if(Header(request.value,WINHTTP_QUERY_CONTENT_ENCODING,encoding)&&!encoding.empty()&&encoding!=L"identity"){error="Compressed subscription response refused";return false;}
  if(Header(request.value,WINHTTP_QUERY_CONTENT_LENGTH,length)){uint64_t count=0;std::string number;for(wchar_t c:length){if(c<L'0'||c>L'9'){error="Invalid subscription content length";return false;}number+=static_cast<char>(c);}if(!Number(number,count)||count>MaxBody){error="Subscription download exceeds 5 MiB";return false;}}
  char buffer[32768];
  for(;;){if(std::chrono::steady_clock::now()-start>std::chrono::seconds(25)){body.clear();error="Subscription request time budget exceeded";return false;}DWORD read=0;if(!WinHttpReadData(request.value,buffer,sizeof(buffer),&read)){body.clear();error="Subscription body read failed";return false;}if(!read)break;if(body.size()+read>MaxBody){body.clear();error="Subscription download exceeds 5 MiB";return false;}body.append(buffer,read);}
  return true;
}
FilterSubscriptions::FilterSubscriptions(std::filesystem::path profile,std::shared_ptr<const PublicSuffix> suffix,Fetch fetch):profile_(std::move(profile)),suffix_(std::move(suffix)),fetch_(fetch?std::move(fetch):Download){
  safe_=SafeProfile(profile_);std::string state;bool state_ok=false;auto read=safe_?ReadStateFile(profile_/L"shield-subscriptions.state",state,64):StateRead::Error;
  state_ok=read==StateRead::Missing||state=="TURAN-SUBSCRIPTIONS-1\n0\n0\n"||state=="TURAN-SUBSCRIPTIONS-1\n1\n0\n"||state=="TURAN-SUBSCRIPTIONS-1\n0\n1\n"||state=="TURAN-SUBSCRIPTIONS-1\n1\n1\n";
  for(size_t i=0;i<items_.size();++i){auto& item=items_[i];const auto& d=Catalog()[i];item.status.id=d.id;item.status.name=d.name;item.status.url=d.url;item.status.license=d.license;
    if(safe_&&!ReadCache(i,L"",item)){
      if(ReadCache(i,L".bak",item)){item.status.recovered=true;item.status.error="Using validated backup; rejected current cache preserved";}
      else {std::string rejected;if(ReadStateFile(Cache(profile_,i),rejected,MaxBody+256)!=StateRead::Missing)item.status.error="Invalid subscription cache preserved; no valid backup";}
    }
    if(state_ok&&read==StateRead::Read)item.status.enabled=state[22+i*2]=='1';
    if(!safe_)item.status.error="Unsafe or unavailable profile directory; subscriptions disabled";else if(!state_ok)item.status.error="Invalid subscription consent state preserved; subscriptions disabled";
  }
}
FilterSubscriptions::~FilterSubscriptions(){stopping_=true;if(worker_.valid())worker_.wait();}
bool FilterSubscriptions::Validate(const std::string& text,const FilterSubscriptionDefinition& definition,FilterReport& report,std::string& error) const{
  if(text.empty()||text.size()>MaxBody||text.find('\0')!=std::string::npos||std::count(text.begin(),text.end(),'\n')>150000||!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0)){error="Invalid subscription size/UTF-8/line count";return false;}
  if(text.rfind("[Adblock Plus",0)!=0||text.find('\n')>100){error="Subscription lacks an Adblock Plus list header";return false;}
  auto engine=FilterEngine::Compile({{definition.name,text,definition.category}},suffix_,report);
  if(!engine||!report.ok||report.network+report.cosmetic==0){error=report.error.empty()?"Subscription has no usable validated rules":report.error;return false;}
  // Upstream lists contain unsupported dialects. Skip whole constrained rules;
  // never silently relax them. Reject grossly malformed/HTML/error responses.
  auto total=report.network+report.cosmetic+report.unsupported+report.invalid;
  if(report.invalid>1000||report.invalid*100>total*5||report.unsupported*100>total*50){error="Subscription rejected: excessive invalid or unsupported rules";return false;}return true;
}
bool FilterSubscriptions::ReadCache(size_t index,const wchar_t* suffix,Item& item) const{
  std::string packed;if(ReadStateFile(Cache(profile_,index,suffix),packed,MaxBody+256)!=StateRead::Read)return false;
  if(packed.rfind("TURAN-FILTER-CACHE-1\n",0)!=0)return false;auto first=packed.find('\n',21),second=first==std::string::npos?first:packed.find('\n',first+1);uint64_t updated=0;
  if(first==std::string::npos||second==std::string::npos||!Number(packed.substr(21,first-21),updated)||updated>Now()+300)return false;
  auto text=packed.substr(second+1),hash=Hash(text);if(hash.empty()||packed.substr(first+1,second-first-1)!=hash)return false;
  FilterReport report;std::string error;if(!Validate(text,Catalog()[index],report,error))return false;
  item.text=std::move(text);item.status.cached=true;item.status.updated=updated;item.status.next_attempt=updated+Day;item.status.sha256=hash;item.status.bytes=item.text.size();item.status.network=report.network;item.status.cosmetic=report.cosmetic;item.status.invalid=report.invalid;item.status.unsupported=report.unsupported;return true;
}
bool FilterSubscriptions::SetEnabled(const std::string& id,bool enabled,bool consent,std::string& error){
  std::lock_guard<std::mutex> lock(mutex_);error.clear();auto found=std::find_if(items_.begin(),items_.end(),[&](const auto& i){return i.status.id==id;});if(found==items_.end()){error="Unknown subscription";return false;}if(enabled&&!consent){error="Explicit native consent is required before subscription network requests";return false;}if(!safe_||!SafeProfile(profile_)){error="Unsafe subscription profile path";return false;}
  std::string state="TURAN-SUBSCRIPTIONS-1\n";for(const auto& item:items_)state+=(item.status.id==id?enabled:item.status.enabled)?"1\n":"0\n";
  if(!WriteStateFile(profile_/L"shield-subscriptions.state",state)){error="Cannot atomically save subscription consent";return false;}found->status.enabled=enabled;found->status.next_attempt=0;++revision_;return true;
}
bool FilterSubscriptions::RefreshAsync(bool force,uint64_t now){
  if(!now)now=Now();std::lock_guard<std::mutex> lock(mutex_);if(busy_||!safe_)return false;bool due=false;for(const auto& item:items_)if(item.status.enabled&&(force||now>=item.status.next_attempt))due=true;if(!due)return false;
  busy_=true;try{worker_=std::async(std::launch::async,[this,force,now]{try{Refresh(force,now);}catch(...){std::lock_guard<std::mutex> guard(mutex_);for(auto& item:items_)if(item.status.updating){item.status.updating=false;item.status.error="Subscription worker failed; previous cache retained";item.status.next_attempt=now+Retry;}busy_=false;++revision_;}});}catch(...){busy_=false;return false;}return true;
}
void FilterSubscriptions::Refresh(bool force,uint64_t now){
  for(size_t i=0;i<items_.size()&&!stopping_;++i){bool run=false;{std::lock_guard<std::mutex> lock(mutex_);auto& s=items_[i].status;run=s.enabled&&(force||now>=s.next_attempt);if(run)s.updating=true;}if(!run)continue;
    std::string text,error;FilterReport report;bool ok=false;try{ok=fetch_(Catalog()[i].url,text,error)&&Validate(text,Catalog()[i],report,error);}catch(...){error="Subscription worker failed";}
    std::lock_guard<std::mutex> lock(mutex_);auto& item=items_[i];item.status.updating=false;item.status.next_attempt=now+(ok?Day:Retry);
    if(ok&&!item.status.enabled)continue;
    if(ok){if(!SafeProfile(profile_)){ok=false;error="Subscription profile directory changed";}else{std::string previous;if(ReadStateFile(Cache(profile_,i),previous,MaxBody+256)==StateRead::Read&&item.status.cached&&!WriteStateFile(Cache(profile_,i,L".bak"),Pack(item.text,item.status.updated))){ok=false;error="Cannot preserve last-known-good subscription";}if(ok&&!WriteStateFile(Cache(profile_,i),Pack(text,now))){ok=false;error="Cannot atomically publish subscription cache";}}}
    if(ok){item.text=std::move(text);item.status.cached=true;item.status.recovered=false;item.status.updated=now;item.status.sha256=Hash(item.text);item.status.bytes=item.text.size();item.status.network=report.network;item.status.cosmetic=report.cosmetic;item.status.unsupported=report.unsupported;item.status.invalid=report.invalid;}
    item.status.error=error;++revision_;
  }std::lock_guard<std::mutex> lock(mutex_);busy_=false;
}
bool FilterSubscriptions::Busy() const{std::lock_guard<std::mutex> lock(mutex_);return busy_;}
uint64_t FilterSubscriptions::Revision() const{std::lock_guard<std::mutex> lock(mutex_);return revision_;}
std::vector<FilterSubscriptionStatus> FilterSubscriptions::Snapshot() const{std::lock_guard<std::mutex> lock(mutex_);std::vector<FilterSubscriptionStatus> result;for(const auto& item:items_)result.push_back(item.status);return result;}
std::vector<FilterSource> FilterSubscriptions::Sources() const{std::lock_guard<std::mutex> lock(mutex_);std::vector<FilterSource> sources;for(const auto& item:items_)if(item.status.enabled&&item.status.cached)sources.push_back({item.status.name,item.text,Catalog()[&item-&items_[0]].category});return sources;}
bool FilterSubscriptions::Rollback(const std::string& id,std::string& error){
  std::lock_guard<std::mutex> lock(mutex_);error.clear();if(busy_||!safe_||!SafeProfile(profile_)){error="Subscription update active or profile unsafe";return false;}size_t i=0;for(;i<items_.size()&&items_[i].status.id!=id;++i);if(i==items_.size()){error="Unknown subscription";return false;}
  Item previous=items_[i];if(!ReadCache(i,L".bak",previous)){error="No validated subscription backup";return false;}
  if(!WriteStateFile(Cache(profile_,i),Pack(previous.text,previous.status.updated))){error="Cannot atomically restore subscription backup";return false;}previous.status.error="Rolled back to validated previous filter list";previous.status.recovered=true;previous.status.next_attempt=Now()+Day;items_[i]=std::move(previous);++revision_;return true;
}
}
