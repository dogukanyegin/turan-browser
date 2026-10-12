#include "update/updater.h"
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cstring>
#include <string>

namespace {
std::wstring Wide(const std::string& text) {
  int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
  std::wstring result(size,L'\0');if(size)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),size);return result;
}
bool WaitParent(DWORD id,const std::wstring& stamp,const std::filesystem::path& app) {
  HANDLE process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,id);if(!process)return false;
  FILETIME creation={},exit={},kernel={},user={};wchar_t executable[32768];DWORD length=32768;ULARGE_INTEGER created={};
  bool valid=GetProcessTimes(process,&creation,&exit,&kernel,&user)&&QueryFullProcessImageNameW(process,0,executable,&length);
  created.LowPart=creation.dwLowDateTime;created.HighPart=creation.dwHighDateTime;
  std::filesystem::path expected=app/L"TuranBrowser.exe";
  if(valid)valid=stamp==std::to_wstring(created.QuadPart)&&_wcsicmp(executable,expected.c_str())==0&&id!=GetCurrentProcessId();
  if(valid)valid=WaitForSingleObject(process,120000)==WAIT_OBJECT_0;CloseHandle(process);return valid;
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int) {
  int count=0;wchar_t** args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args)return 2;
  std::filesystem::path app,stage;bool apply=false,recover=false;DWORD parent=0;std::wstring stamp;bool valid=true;
  for(int i=1;i<count;++i) {
    std::wstring option=args[i];
    if(option==L"--apply"&&!apply&&!recover)apply=true;
    else if(option==L"--recover"&&!apply&&!recover)recover=true;
    else if(option==L"--app"&&i+1<count&&app.empty())app=args[++i];
    else if(option==L"--stage"&&i+1<count&&stage.empty())stage=args[++i];
    else if(option==L"--parent"&&i+1<count&&!parent) {
      wchar_t* end=nullptr;auto n=wcstoul(args[++i],&end,10);if(!end||*end||!n||n==ULONG_MAX)valid=false;else parent=n;
    }
    else if(option==L"--parent-created"&&i+1<count&&stamp.empty())stamp=args[++i];
    else valid=false;
  }
  LocalFree(args);auto policy=turan::update::PublisherPolicy();
  if(!valid||app.empty()||(!apply&&!recover)||(apply&&stage.empty())||(recover&&!stage.empty())||static_cast<bool>(parent)!=!stamp.empty())return 2;
  // There is no unsigned update / test-key override command-line option.
  if(!turan::update::Configured(policy))return 3;
  if(MessageBoxW(nullptr,apply?L"Apply the authenticated Turan Browser update? Close all Turan windows before continuing. Your profiles and saved files will be preserved.":L"Recover an interrupted authenticated Turan Browser update? Close all Turan windows before continuing.",
      L"Turan Browser - secure updater",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)return 4;
  if(parent&&!WaitParent(parent,stamp,app)) {
    MessageBoxW(nullptr,L"The initiating Turan process identity could not be verified or did not exit in time. No update has been applied.",L"Turan Browser - secure updater",MB_OK|MB_ICONERROR);return 5;
  }
  std::string error;bool ok=false;
  try {ok=recover?turan::update::Recover(app,policy,error):turan::update::Apply(app,stage,policy,error);}
  catch(...) {
    MessageBoxW(nullptr,L"The updater could not complete. The transaction journal and backups have been preserved for recovery. No profile data is an update target.",
        L"Turan Browser - secure updater",MB_OK|MB_ICONERROR);return 6;
  }
  MessageBoxW(nullptr,ok?L"The authenticated runtime update/recovery completed. User profiles have been preserved. Restart Turan Browser to continue.":Wide(error).c_str(),
      L"Turan Browser - secure updater",MB_OK|(ok?MB_ICONINFORMATION:MB_ICONERROR));
  return ok?0:6;
}
