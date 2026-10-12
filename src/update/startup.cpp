// CEF-free startup guard. The OS must be able to load this executable even
// while TuranBrowser.dll is temporarily absent during an interrupted update.
#include "update/updater.h"
#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <string>
#include <climits>
#include <cwchar>

namespace {
std::wstring Utf8(const std::string& s) {
  if(s.empty())return {};
  if(s.size()>static_cast<size_t>(INT_MAX))return L"Invalid recovery error length.";
  int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
  if(length<=0)return L"Startup recovery failed (invalid error encoding).";
  std::wstring result(static_cast<size_t>(length),L'\0');
  MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),result.data(),length);
  return result;
}
std::wstring Quote(const std::wstring& s) {
  std::wstring result=L"\"";size_t backslashes=0;
  for(wchar_t c:s) {
    if(c==L'\\'){++backslashes;continue;}
    if(c==L'"'){result.append(backslashes*2+1,L'\\');result+=c;backslashes=0;continue;}
    result.append(backslashes,L'\\');backslashes=0;result+=c;
  }
  result.append(backslashes*2,L'\\');return result+L'"';
}
bool RegularFile(const std::filesystem::path& path) {
  DWORD attrs=GetFileAttributesW(path.c_str());
  return attrs!=INVALID_FILE_ATTRIBUTES && !(attrs&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
}
void Error(const std::wstring& msg,bool check_only) {
  if(!check_only)MessageBoxW(nullptr,msg.c_str(),L"Turan Browser - startup recovery",MB_OK|MB_ICONERROR);
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int) {
  int argc=0;wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);
  if(!argv)return 10;
  // The check-only option makes the real CEF-free recovery path testable
  // without launching Chromium or requiring any user interface automation.
  const bool check_only=argc==2 && std::wstring(argv[1])==L"--check-only";
  wchar_t name[32768]={};DWORD length=GetModuleFileNameW(nullptr,name,32768);
  if(!length||length>=32768){LocalFree(argv);Error(L"Cannot resolve the startup executable.",check_only);return 11;}
  const std::filesystem::path self(name),app=self.parent_path();
  if(_wcsicmp(self.filename().c_str(),L"TuranStartup.exe")!=0 ||
     (GetFileAttributesW(app.c_str())&FILE_ATTRIBUTE_REPARSE_POINT)) {
    LocalFree(argv);Error(L"Unsafe startup path or executable name.",check_only);return 12;
  }
  // Recover before testing for DLL presence: recovery is most urgently needed
  // when power loss left the DLL missing between backup and promotion.
  auto policy=turan::update::PublisherPolicy();std::string problem;
  bool recovered=false;
  try {recovered=turan::update::Recover(app,policy,problem);} catch(...) {problem="Unexpected recovery failure.";}
  if(!recovered) {
    LocalFree(argv);
    Error(L"Turan Browser could not safely recover a pending update. The transaction and user data were preserved. Do not bypass recovery by running TuranBrowser.exe directly.\n\n"+Utf8(problem),check_only);
    return 13;
  }
  const auto runtime=app/L"TuranBrowser.exe";
  if(!RegularFile(runtime)||!RegularFile(app/L"TuranBrowser.dll")||!RegularFile(app/L"libcef.dll")) {
    LocalFree(argv);Error(L"The Turan runtime is missing or redirected. Startup was refused without touching user profiles.",check_only);
    return 14;
  }
  if(check_only){LocalFree(argv);return 0;}
  std::wstring command=Quote(runtime.wstring());
  for(int i=1;i<argc;++i){command+=L" ";command+=Quote(argv[i]);}
  LocalFree(argv);
  STARTUPINFOW startup={sizeof(startup)};PROCESS_INFORMATION child={};
  // Do not inject any special CEF flags. Renderer subprocesses remain owned
  // by the original CEF bootstrap, not this recovery-only launcher.
  if(!CreateProcessW(runtime.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,app.c_str(),&startup,&child)) {
    Error(L"Windows could not start TuranBrowser.exe. No profile data was changed by the launcher.",false);
    return 15;
  }
  CloseHandle(child.hThread);CloseHandle(child.hProcess);
  return 0;
}
