#include "core/state_file.h"
#include <windows.h>
#include <algorithm>
#include <atomic>

namespace turan {
namespace {
std::atomic<unsigned> sequence{0};
std::filesystem::path Unique(const std::filesystem::path& path,const wchar_t* suffix) {
  FILETIME time;GetSystemTimeAsFileTime(&time);
  ULARGE_INTEGER stamp;stamp.LowPart=time.dwLowDateTime;stamp.HighPart=time.dwHighDateTime;
  return path.parent_path()/(path.filename().wstring()+suffix+std::to_wstring(stamp.QuadPart)+L"-"+
      std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++sequence));
}
bool PlainOrMissing(const std::filesystem::path& path) {
  DWORD attributes=GetFileAttributesW(path.c_str());
  if(attributes==INVALID_FILE_ATTRIBUTES) {
    DWORD error=GetLastError();return error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND;
  }
  return !(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
}
}
StateRead ReadStateFile(const std::filesystem::path& path,std::string& bytes,size_t limit) {
  bytes.clear();
  HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
  if(file==INVALID_HANDLE_VALUE) {
    DWORD error=GetLastError();
    return error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND?StateRead::Missing:StateRead::Error;
  }
  BY_HANDLE_FILE_INFORMATION information={};LARGE_INTEGER size={};
  bool ok=GetFileInformationByHandle(file,&information)&&
      !(information.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&
      GetFileSizeEx(file,&size)&&size.QuadPart>=0&&static_cast<unsigned long long>(size.QuadPart)<=limit;
  if(ok) {
    bytes.resize(static_cast<size_t>(size.QuadPart));size_t offset=0;
    while(offset<bytes.size()) {
      DWORD read=0,count=static_cast<DWORD>(std::min<size_t>(65536,bytes.size()-offset));
      if(!ReadFile(file,bytes.data()+offset,count,&read,nullptr)||!read){ok=false;break;}
      offset+=read;
    }
  }
  CloseHandle(file);if(!ok)bytes.clear();return ok?StateRead::Read:StateRead::Error;
}
bool WriteStateFile(const std::filesystem::path& path,const std::string& bytes) {
  if(!PlainOrMissing(path))return false;
  auto temporary=Unique(path,L".tmp-");
  HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file==INVALID_HANDLE_VALUE)return false;
  bool ok=true;size_t offset=0;
  while(offset<bytes.size()) {
    DWORD written=0,count=static_cast<DWORD>(std::min<size_t>(65536,bytes.size()-offset));
    if(!WriteFile(file,bytes.data()+offset,count,&written,nullptr)||!written){ok=false;break;}
    offset+=written;
  }
  if(ok)ok=FlushFileBuffers(file)!=FALSE;
  if(!CloseHandle(file))ok=false;
  if(ok)ok=PlainOrMissing(path)&&MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
  if(!ok)DeleteFileW(temporary.c_str());return ok;
}
bool QuarantineStateFile(const std::filesystem::path& path) {
  if(!PlainOrMissing(path))return false;
  DWORD attributes=GetFileAttributesW(path.c_str());
  if(attributes==INVALID_FILE_ATTRIBUTES)return GetLastError()==ERROR_FILE_NOT_FOUND;
  auto rejected=Unique(path,L".rejected-");
  return MoveFileExW(path.c_str(),rejected.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;
}
}
