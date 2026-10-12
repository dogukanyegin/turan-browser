#include "privacy/tor_browser.h"
#include "core/model.h"
#include "core/state_file.h"
#include <windows.h>
#include <winhttp.h>
#include <shlobj.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <iomanip>
#include <map>
#include <sstream>
#include <memory>

namespace turan {
namespace {
struct CatalogFile {const wchar_t* name;uint64_t bytes;const char* sha;};
#include "privacy/tor_catalog.inc"
struct Handle {
  HANDLE value=INVALID_HANDLE_VALUE;
  explicit Handle(HANDLE h):value(h){}
  ~Handle(){if(value!=INVALID_HANDLE_VALUE&&value)CloseHandle(value);}
};
struct Verified {
  std::filesystem::path root;
  std::vector<std::unique_ptr<Handle>> handles;
};
std::wstring Fold(std::wstring value) {
  if(!value.empty())CharLowerBuffW(value.data(),static_cast<DWORD>(value.size()));return value;
}
bool PlainPath(const std::filesystem::path& path,bool directory) {
  auto attributes=GetFileAttributesW(path.c_str());
  return attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT)&&
      ((attributes&FILE_ATTRIBUTE_DIRECTORY)!=0)==directory;
}
bool RootPath(const std::filesystem::path& input,std::filesystem::path& output) {
  // No UNC/device/relative paths or NTFS alternate streams. Pin an exact local root.
  auto text=input.wstring();
  if(text.size()<4||text.size()>8192||text[1]!=L':'||(text[2]!=L'\\'&&text[2]!=L'/')||
      !((text[0]>=L'A'&&text[0]<=L'Z')||(text[0]>=L'a'&&text[0]<=L'z'))||text.find(L':',2)!=std::wstring::npos)return false;
  for(auto c:text)if(c<32||c==127)return false;
  output=input.lexically_normal();
  while(output.filename().empty()&&output!=output.root_path())output=output.parent_path();
  if(output==output.root_path()||!PlainPath(output,true))return false;
  for(auto part=output;!part.empty()&&part!=part.parent_path();part=part.parent_path())if(!PlainPath(part,true))return false;
  return true;
}
std::string Digest(HANDLE file,uint64_t expected) {
  LARGE_INTEGER size;if(!GetFileSizeEx(file,&size)||size.QuadPart<0||static_cast<uint64_t>(size.QuadPart)!=expected)return {};
  BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
  if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
  DWORD object_size=0,returned=0;
  bool ok=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&object_size),sizeof(object_size),&returned,0)>=0;
  std::vector<unsigned char> object(object_size);std::array<unsigned char,65536> buffer;std::array<unsigned char,32> digest;
  ok=ok&&BCryptCreateHash(algorithm,&hash,object.data(),object_size,nullptr,0,0)>=0;
  uint64_t read_total=0;
  while(ok) {
    DWORD count=0;if(!ReadFile(file,buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr)){ok=false;break;}
    if(!count)break;read_total+=count;
    if(read_total>expected||BCryptHashData(hash,buffer.data(),count,0)<0){ok=false;break;}
  }
  ok=ok&&read_total==expected&&BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
  if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
  if(!ok)return {};std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto byte:digest)out<<std::setw(2)<<static_cast<unsigned>(byte);return out.str();
}
TorBrowserResult Verify(const std::filesystem::path& input,Verified& verified) {
  TorBrowserResult result;
  if(!RootPath(input,verified.root)){result.error="Select a regular local Tor Browser installation folder; missing, redirected or network paths are refused.";return result;}
  auto fail=[&](){result.error="Tor Browser files do not match the authenticated release catalog (15.0.24). Updated/other versions need a new verified catalog. No application was launched.";return result;};
  std::map<std::wstring,const CatalogFile*> expected;
  // Keep code open without write/delete sharing until CreateProcess returns.
  for(const auto& item:kTorCatalog) {
    auto path=verified.root/item.name;expected.emplace(Fold(item.name),&item);
    if(!PlainPath(path,false))return fail();
    for(auto parent=path.parent_path();parent!=verified.root;parent=parent.parent_path())
      if(parent==parent.parent_path()||!PlainPath(parent,true))return fail();
    auto file=std::make_unique<Handle>(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
    BY_HANDLE_FILE_INFORMATION info={};
    if(file->value==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(file->value,&info)||
        (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||Digest(file->value,item.bytes)!=item.sha)return fail();
    verified.handles.push_back(std::move(file));++result.verified_files;
  }
  // Unexpected loadable code beside the verified engine must not be silently accepted.
  // The external browser's own profile/Data and updater staging are its responsibility;
  // Turan never traverses or changes these mutable user directories.
  std::error_code error;std::filesystem::recursive_directory_iterator it(verified.root/L"Browser",error),end;
  size_t entries=0;
  while(!error&&it!=end) {
    if(++entries>4096||it.depth()>16)return fail();
    auto relative=it->path().lexically_relative(verified.root);auto name=Fold(relative.generic_wstring());
    auto attrs=GetFileAttributesW(it->path().c_str());if(attrs==INVALID_FILE_ATTRIBUTES)return fail();
    bool mutable_dir=name==L"browser/torbrowser/data"||name==L"browser/updates";
    if(attrs&FILE_ATTRIBUTE_DIRECTORY) {
      if(mutable_dir)it.disable_recursion_pending();
      else if(attrs&FILE_ATTRIBUTE_REPARSE_POINT)return fail();
    } else {
      if(attrs&FILE_ATTRIBUTE_REPARSE_POINT)return fail();
      auto ext=Fold(relative.extension().wstring());
      if(ext==L".exe"||ext==L".dll"||ext==L".ja"||ext==L".js"||ext==L".jsc"||ext==L".cfg"||ext==L".xpi") {
        std::replace(name.begin(),name.end(),L'/',L'\\');if(!expected.count(name))return fail();
      }
    }
    it.increment(error);
  }
  if(error)return fail();result.ok=true;return result;
}
std::filesystem::path Known(REFKNOWNFOLDERID id) {
  PWSTR path=nullptr;if(SHGetKnownFolderPath(id,KF_FLAG_DONT_VERIFY,nullptr,&path)!=S_OK)return {};
  std::filesystem::path result(path);CoTaskMemFree(path);return result;
}
}
const char* VerifiedTorBrowserVersion(){return "15.0.24";}
bool TorTargetUrl(const std::string& url,std::wstring& target,std::string& error) {
  target.clear();error="Only an HTTP(S) page without embedded credentials can be opened in Tor Browser.";
  if(url.empty()||url.size()>16384)return false;
  for(unsigned char c:url)if(c<=32||c>=127||c=='\\'||c=='"')return false;
  auto wide=Wide(url);URL_COMPONENTS parts={sizeof(parts)};
  parts.dwSchemeLength=parts.dwHostNameLength=parts.dwUserNameLength=parts.dwPasswordLength=static_cast<DWORD>(-1);
  if(!WinHttpCrackUrl(wide.c_str(),static_cast<DWORD>(wide.size()),0,&parts)||
      (parts.nScheme!=INTERNET_SCHEME_HTTP&&parts.nScheme!=INTERNET_SCHEME_HTTPS)||
      !parts.dwHostNameLength||parts.dwUserNameLength||parts.dwPasswordLength)return false;
  // Do not depend on parser-specific handling of an empty userinfo separator.
  auto authority=url.find("://");if(authority==std::string::npos)return false;
  auto authority_end=url.find_first_of("/?#",authority+3);
  if(url.substr(authority+3,authority_end==std::string::npos?std::string::npos:authority_end-authority-3).find('@')!=std::string::npos)return false;
  target=std::move(wide);error.clear();return true;
}
std::wstring QuoteProcessArgument(const std::wstring& argument) {
  std::wstring out=L"\"";size_t slashes=0;
  for(wchar_t ch:argument) {
    if(ch==L'\\'){++slashes;continue;}
    if(ch==L'"'){out.append(slashes*2+1,L'\\');out+=ch;}
    else {out.append(slashes,L'\\');out+=ch;}
    slashes=0;
  }
  out.append(slashes*2,L'\\');out+=L'"';return out;
}
std::filesystem::path TorBrowserPreference(const std::filesystem::path& profile) {
  std::string bytes;std::istringstream in;
  if(ReadStateFile(profile/L"tor-browser-path.txt",bytes,32768)!=StateRead::Read||bytes.find('\0')!=std::string::npos)return {};
  in.str(bytes);std::string magic,value,extra;unsigned schema=0;
  if(!(in>>magic>>schema>>std::quoted(value))||magic!="TURAN_TOR_BROWSER"||schema!=1||(in>>extra))return {};
  auto wide=Wide(value);if(Utf8(wide)!=value)return {};std::filesystem::path root;
  return RootPath(wide,root)?root:std::filesystem::path();
}
bool SaveTorBrowserPreference(const std::filesystem::path& profile,const std::filesystem::path& input) {
  std::filesystem::path root;if(!RootPath(input,root))return false;
  std::ostringstream out;out<<"TURAN_TOR_BROWSER 1 "<<std::quoted(Utf8(root.wstring()))<<'\n';
  return WriteStateFile(profile/L"tor-browser-path.txt",out.str());
}
std::vector<std::filesystem::path> TorBrowserCandidates(const std::filesystem::path& profile) {
  std::vector<std::filesystem::path> out;
  auto add=[&](const std::filesystem::path& input){std::filesystem::path root;if(RootPath(input,root)&&PlainPath(root/L"Browser"/L"firefox.exe",false)&&
      std::none_of(out.begin(),out.end(),[&](const auto& old){return Fold(old.wstring())==Fold(root.wstring());}))out.push_back(root);};
  add(TorBrowserPreference(profile));
  for(auto id:{&FOLDERID_Desktop,&FOLDERID_LocalAppData,&FOLDERID_ProgramFiles,&FOLDERID_Downloads}) {
    auto path=Known(*id);if(!path.empty())add(path/L"Tor Browser");
  }
  return out;
}
TorBrowserResult VerifyTorBrowser(const std::filesystem::path& root){Verified verified;return Verify(root,verified);}
TorBrowserResult OpenInTorBrowser(const std::filesystem::path& root,const std::string& url) {
  std::wstring target;TorBrowserResult result;if(!TorTargetUrl(url,target,result.error))return result;
  Verified verified;result=Verify(root,verified);if(!result.ok)return result;
  auto exe=verified.root/L"Browser"/L"firefox.exe";
  auto command=QuoteProcessArgument(exe.wstring())+L" --new-tab "+QuoteProcessArgument(target);
  if(command.size()>=32767){result.ok=false;result.error="The Tor Browser launch command exceeds the Windows limit.";return result;}
  STARTUPINFOW startup={sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_SHOWNORMAL;PROCESS_INFORMATION process={};
  result.ok=CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,exe.parent_path().c_str(),&startup,&process)!=FALSE;
  if(!result.ok){result.error="Windows could not start the verified Tor Browser (error "+std::to_string(GetLastError())+").";return result;}
  result.process_id=process.dwProcessId;CloseHandle(process.hThread);CloseHandle(process.hProcess);return result;
}
}
