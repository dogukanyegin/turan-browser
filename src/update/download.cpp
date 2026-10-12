#include "update/internal.h"
#include <winhttp.h>
#include <algorithm>
#include <chrono>

namespace turan::update {
namespace {
struct Internet {HINTERNET h=nullptr;~Internet(){if(h)WinHttpCloseHandle(h);}};
using Sink=std::function<bool(const void*,DWORD)>;
bool Request(const std::string& url,uint64_t limit,const Sink& sink,std::string& error,const Cancelled& cancelled) {
  error.clear();if(!HttpsUrlValid(url)||limit>2ULL*1024*1024*1024){error="Unsafe update URL or download limit.";return false;}
  auto stop=[&](){if(cancelled&&cancelled()){error="The update operation was cancelled; runtime/profile files were preserved.";return true;}return false;};
  if(stop())return false;
  auto slash=url.find('/',8);std::wstring host(url.begin()+8,url.begin()+slash),path(url.begin()+slash,url.end());
  Internet session;session.h=WinHttpOpen(L"Turan-Authenticated-Updater/1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
  if(!session.h){error="Could not initialize the HTTPS updater.";return false;}
  DWORD tls=WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
  if(!WinHttpSetOption(session.h,WINHTTP_OPTION_SECURE_PROTOCOLS,&tls,sizeof tls)||!WinHttpSetTimeouts(session.h,15000,15000,15000,15000)) {
    error="Could not configure the secure update connection.";return false;
  }
  Internet connection;connection.h=WinHttpConnect(session.h,host.c_str(),INTERNET_DEFAULT_HTTPS_PORT,0);
  Internet request;request.h=connection.h?WinHttpOpenRequest(connection.h,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):nullptr;
  DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER,autologon=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
  DWORD disabled=WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION;
  DWORD enabled=WINHTTP_ENABLE_SSL_REVOCATION;
  if(!request.h||!WinHttpSetOption(request.h,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof redirect)||
     !WinHttpSetOption(request.h,WINHTTP_OPTION_AUTOLOGON_POLICY,&autologon,sizeof autologon)||
     !WinHttpSetOption(request.h,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof disabled)||
     !WinHttpSetOption(request.h,WINHTTP_OPTION_ENABLE_FEATURE,&enabled,sizeof enabled)||
     stop()||!WinHttpSendRequest(request.h,L"Accept: application/octet-stream\r\nCache-Control: no-cache\r\n",static_cast<DWORD>(-1),WINHTTP_NO_REQUEST_DATA,0,0,0)||
     stop()||!WinHttpReceiveResponse(request.h,nullptr)) {
    error="HTTPS update request failed; TLS errors, redirects and authentication are not bypassed.";return false;
  }
  DWORD status=0,size=sizeof status;
  if(!WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX)||status!=200) {
    error="The update server did not return HTTP 200.";return false;
  }
  wchar_t length[64];size=sizeof length;uint64_t expected=0;bool length_known=false;
  if(WinHttpQueryHeaders(request.h,WINHTTP_QUERY_CONTENT_LENGTH,WINHTTP_HEADER_NAME_BY_INDEX,length,&size,WINHTTP_NO_HEADER_INDEX)) {
    size_t digits=wcslen(length);if(!digits||digits>20){error="Invalid update Content-Length.";return false;}
    for(size_t i=0;i<digits;++i){if(length[i]<L'0'||length[i]>L'9'||expected>(UINT64_MAX-(length[i]-L'0'))/10){error="Invalid update Content-Length.";return false;}expected=expected*10+length[i]-L'0';}
    if(expected>limit){error="The update download exceeds its size budget.";return false;}length_known=true;
  }
  auto started=std::chrono::steady_clock::now();uint64_t total=0;unsigned char buffer[65536];
  for(;;) {
    if(stop())return false;
    if(std::chrono::steady_clock::now()-started>std::chrono::minutes(15)){error="The update download exceeded its time budget.";return false;}
    DWORD read=0;if(!WinHttpReadData(request.h,buffer,sizeof buffer,&read)){error="The update stream was interrupted.";return false;}
    if(!read)break;total+=read;
    if(total>limit||!sink(buffer,read)){error="The update stream exceeded its budget or could not be written.";return false;}
  }
  if(stop())return false;
  if(length_known&&total!=expected){error="The update stream length does not match its response header.";return false;}return true;
}
}
bool Download(const std::string& url,const std::filesystem::path& target,uint64_t limit,std::string& error,const Cancelled& cancelled) {
  if(cancelled&&cancelled()){error="The update operation was cancelled.";return false;}
  detail::Handle file(CreateFileW(target.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
  if(!file){error="Could not create the isolated update payload file.";return false;}
  bool ok=Request(url,limit,[&](const void* bytes,DWORD count){DWORD written=0;return WriteFile(file.h,bytes,count,&written,nullptr)&&written==count;},error,cancelled);
  if(ok&&!FlushFileBuffers(file.h)){error="Could not flush the update payload.";ok=false;}file.Close();
  if(!ok)DeleteFileW(target.c_str());return ok;
}
bool DownloadBytes(const std::string& url,uint64_t limit,std::string& bytes,std::string& error,const Cancelled& cancelled) {
  bytes.clear();if(limit>1024*1024){error="The update metadata size budget is invalid.";return false;}
  bool ok=Request(url,limit,[&](const void* data,DWORD count){bytes.append(static_cast<const char*>(data),count);return true;},error,cancelled);
  if(!ok)bytes.clear();return ok;
}
}
