#include <windows.h>
#include <shellapi.h>
#include <map>
#include <regex>
#include "core/profile_backup.h"
#include "core/profile_worker.h"
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
  int count=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&count);if(!argv)return 2;
  std::map<std::wstring,std::wstring> options;bool valid=count==5;
  for(int i=1;i<count;++i) {
    std::wstring argument=argv[i];auto split=argument.find(L'=');
    if(split==std::wstring::npos||split+1==argument.size()||!options.emplace(argument.substr(0,split),argument.substr(split+1)).second)valid=false;
  }
  LocalFree(argv);
  if(!valid||options.size()!=4||!options.count(L"--profile")||!options.count(L"--backup-profile")||!options.count(L"--parent-handle")||!options.count(L"--ready-handle"))return 2;
  auto number=options[L"--parent-handle"];
  if(!std::regex_match(number,std::wregex(L"[0-9]{1,20}")))return 2;
  uintptr_t value=0;try {value=std::stoull(number);}catch(...){return 2;}
  HANDLE parent=reinterpret_cast<HANDLE>(value);DWORD flags=0;
  auto ready_number=options[L"--ready-handle"];if(!std::regex_match(ready_number,std::wregex(L"[0-9]{1,20}")))return 2;
  uintptr_t ready_value=0;try {ready_value=std::stoull(ready_number);}catch(...){return 2;}
  HANDLE ready=reinterpret_cast<HANDLE>(ready_value);
  wchar_t image[32768],own[32768];DWORD length=32768;
  if(!GetHandleInformation(parent,&flags)||!GetHandleInformation(ready,&flags)||GetProcessId(parent)==GetCurrentProcessId()||
     !QueryFullProcessImageNameW(parent,0,image,&length)||!GetModuleFileNameW(nullptr,own,32768))return 2;
  auto expected=std::filesystem::path(own).parent_path()/L"TuranBrowser.exe";
  if(CompareStringOrdinal(image,-1,expected.c_str(),-1,TRUE)!=CSTR_EQUAL)return 2;
  if(!SetEvent(ready))return 2;CloseHandle(ready);
  auto wait=WaitForSingleObject(parent,60000);DWORD exit=1;
  bool closed=wait==WAIT_OBJECT_0&&GetExitCodeProcess(parent,&exit)&&exit==0;CloseHandle(parent);
  turan::ProfileOperation result;
  if(!closed)result.error="The browser did not exit successfully; the profile has not been copied.";
  else {
    std::filesystem::path root=options[L"--profile"];
    turan::ProfileLease lease;std::string error;
    if(!turan::RecoverProfileRestore(root,error)||!lease.Acquire(root,error,false))result.error=error;
    else result=turan::CreateProfileBackup(root,options[L"--backup-profile"],lease);
  }
  turan::ReportProfileOperation(result);return result.ok?0:6;
}
