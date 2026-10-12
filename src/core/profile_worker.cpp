#include "core/profile_worker.h"
#include "core/model.h"
#include <windows.h>
#include <vector>
namespace turan {
void ReportProfileOperation(const ProfileOperation& operation) {
  std::wstring text=operation.ok?L"Full profile operation completed and SHA-256 verified.":L"Profile operation stopped.\n";
  if(!operation.error.empty())text+=L"\n"+Wide(operation.error);
  if(!operation.backup.empty())text+=L"\n\nBackup: "+operation.backup.wstring();
  if(!operation.previous.empty())text+=L"\n\nPrevious profile retained: "+operation.previous.wstring();
  MessageBoxW(nullptr,text.c_str(),L"Turan Browser - profile operation",MB_OK|(operation.ok?MB_ICONINFORMATION:MB_ICONERROR));
}
namespace {
std::wstring Quote(const std::wstring& value) {
  std::wstring result=L"\"";unsigned slashes=0;
  for(auto c:value) {
    if(c==L'\\'){++slashes;continue;}
    result.append(c==L'\"'?slashes*2+1:slashes,L'\\');slashes=0;result+=c;
  }
  result.append(slashes*2,L'\\');return result+L"\"";
}
}
bool StartProfileBackupWorker(const std::filesystem::path& directory,const std::filesystem::path& profile,
                             const std::filesystem::path& destination,std::string& error) {
  auto executable=directory/L"TuranProfileTool.exe";
  DWORD attributes=GetFileAttributesW(executable.c_str());
  if(attributes==INVALID_FILE_ATTRIBUTES||attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) {
    error="The engine-free TuranProfileTool.exe backup utility is missing or redirected.";return false;
  }
  HANDLE parent=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,TRUE,GetCurrentProcessId());
  if(!parent){error="Could not open the browser shutdown handle.";return false;}
  SECURITY_ATTRIBUTES security={sizeof(security),nullptr,TRUE};
  HANDLE ready=CreateEventW(&security,TRUE,FALSE,nullptr);
  if(!ready){CloseHandle(parent);error="Could not create the backup readiness event.";return false;}
  HANDLE inherited[]={parent,ready};
  SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
  std::vector<unsigned char> buffer(bytes);
  STARTUPINFOEXW startup={};startup.StartupInfo.cb=sizeof(startup);
  startup.StartupInfo.dwFlags=STARTF_USESHOWWINDOW;startup.StartupInfo.wShowWindow=SW_HIDE;
  startup.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
  bool initialized=InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&bytes)!=FALSE;
  bool configured=initialized&&UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr);
  auto command=Quote(executable.wstring())+L" "+Quote(L"--profile="+profile.wstring())+
      L" "+Quote(L"--backup-profile="+destination.wstring())+L" --parent-handle="+std::to_wstring(reinterpret_cast<uintptr_t>(parent))+
      L" --ready-handle="+std::to_wstring(reinterpret_cast<uintptr_t>(ready));
  PROCESS_INFORMATION process={};
  bool started=configured&&CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,
      EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW,nullptr,directory.c_str(),&startup.StartupInfo,&process);
  if(initialized)DeleteProcThreadAttributeList(startup.lpAttributeList);CloseHandle(parent);
  if(!started){CloseHandle(ready);error="Could not start the closed-profile backup utility.";return false;}
  HANDLE waiting[]={ready,process.hProcess};
  // The browser remains alive until the worker has verified its inherited
  // process handle. Image-name queries can fail after process teardown.
  bool acknowledged=WaitForMultipleObjects(2,waiting,FALSE,5000)==WAIT_OBJECT_0;
  CloseHandle(ready);CloseHandle(process.hThread);CloseHandle(process.hProcess);
  if(!acknowledged)error="The backup utility did not acknowledge a valid browser shutdown handle.";
  return acknowledged;
}
}
