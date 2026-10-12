#include "update/worker.h"
#include "update/internal.h"
#include <windows.h>

namespace turan::update {
namespace {
std::wstring Quote(const std::wstring& text) {
  std::wstring out=L"\"";size_t slashes=0;
  for(wchar_t c:text) {
    if(c==L'\\'){++slashes;continue;}
    if(c==L'\"'){out.append(slashes*2+1,L'\\');out+=L'\"';slashes=0;continue;}
    out.append(slashes,L'\\');slashes=0;out+=c;
  }
  out.append(slashes*2,L'\\');out+=L'\"';return out;
}
}
bool StartWorker(const std::filesystem::path& app,const std::filesystem::path& stage,std::string& error) {
  auto policy=PublisherPolicy();if(!Configured(policy)){error="No authenticated NorthByte update channel has been provisioned.";return false;}
  std::filesystem::path root,staging;
  if(!detail::Normalize(app,root,error)||!detail::Normalize(stage,staging,error)||
     detail::RootId(root.parent_path())!=detail::RootId(staging.parent_path()))return false;
  auto name=staging.filename().string();
  if(name.size()!=46||name.rfind(".turan-update-",0)!=0||name.substr(14).find_first_not_of("0123456789abcdef")!=std::string::npos||
     !detail::Missing(root/L".turan-update-transaction")) {
    error="The updater helper requires a fresh owned stage and no pending transaction.";return false;
  }
  std::error_code enumeration_error;
  std::filesystem::directory_iterator iterator(staging,enumeration_error),end;
  if(enumeration_error){error="The isolated helper directory cannot be enumerated.";return false;}
  for(;iterator!=end;iterator.increment(enumeration_error)) {
    if(enumeration_error){error="The isolated helper directory could not be verified.";return false;}
    auto leaf=iterator->path().filename().wstring();
    bool payload=leaf==L"payload";
    if((leaf!=L"owner.txt"&&leaf!=L"manifest.txt"&&leaf!=L"manifest.sig"&&!payload)||!detail::Plain(iterator->path(),payload)) {
      error="An unexpected or redirected file in the isolated helper directory was refused.";return false;
    }
  }
  std::string id,metadata,signature;Manifest manifest;
  if(!detail::Read(staging/L"owner.txt",128,id)||id!=detail::RootId(root)+"\n"||
     !detail::Read(staging/L"manifest.txt",1024*1024,metadata)||!detail::Read(staging/L"manifest.sig",1024,signature)||
     !Authenticate(policy,metadata,signature,manifest,error)) {
    if(error.empty())error="The prepared update cannot be authenticated.";return false;
  }
  wchar_t executable[32768];DWORD length=GetModuleFileNameW(nullptr,executable,32768);
  FILETIME creation={},exit={},kernel={},user={};
  if(!length||length>=32768||_wcsicmp(executable,(root/L"TuranBrowser.exe").c_str())||
     !GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user)) {
    error="The update must be initiated by this Turan Browser executable.";return false;
  }
  std::vector<detail::Handle> guards;
  if(!detail::Parents(root,guards)||!detail::Parents(staging,guards)){error="The update helper path is redirected.";return false;}
  std::string bytes;
  if(!detail::Read(root/L"TuranUpdateTool.exe",4*1024*1024,bytes)||bytes.empty()) {
    error="The currently installed secure update helper is missing or unreadable.";return false;
  }
  auto helper=staging/L"apply-helper.exe";
  if(!detail::Write(helper,bytes)){error="The isolated update helper already exists or could not be written.";return false;}
  auto owned=detail::Open(helper);uint64_t size=0;std::string sha;
  if(!owned||!detail::Hash(owned.h,size,sha)||size!=bytes.size()||sha!=Sha256(bytes)) {
    error="The isolated update helper integrity check failed.";return false;
  }
  ULARGE_INTEGER stamp;stamp.LowPart=creation.dwLowDateTime;stamp.HighPart=creation.dwHighDateTime;
  std::wstring command=Quote(helper.wstring())+L" --apply --app "+Quote(root.wstring())+L" --stage "+Quote(staging.wstring())+
      L" --parent "+std::to_wstring(GetCurrentProcessId())+L" --parent-created "+std::to_wstring(stamp.QuadPart);
  STARTUPINFOW startup={sizeof startup};PROCESS_INFORMATION process={};
  if(!CreateProcessW(helper.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,staging.c_str(),&startup,&process)) {
    error="The secure updater helper could not be started.";return false;
  }
  CloseHandle(process.hThread);CloseHandle(process.hProcess);error.clear();return true;
}
}
