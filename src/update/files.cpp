#include "update/internal.h"
#include <bcrypt.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace turan::update {
namespace {
struct Hasher {
  BCRYPT_ALG_HANDLE provider=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::vector<unsigned char> object;
  ~Hasher(){if(hash)BCryptDestroyHash(hash);if(provider)BCryptCloseAlgorithmProvider(provider,0);}
  bool Start() {
    DWORD size=0,result=0;
    if(BCryptOpenAlgorithmProvider(&provider,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0||
       BCryptGetProperty(provider,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof size,&result,0)<0)return false;
    object.resize(size);return BCryptCreateHash(provider,&hash,object.data(),size,nullptr,0,0)>=0;
  }
  bool Add(const void* data,size_t size){return size<=ULONG_MAX&&BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<void*>(data)),static_cast<ULONG>(size),0)>=0;}
  std::string End(){unsigned char digest[32];if(BCryptFinishHash(hash,digest,32,0)<0)return {};std::string out;out.reserve(64);for(auto b:digest){out+="0123456789abcdef"[b>>4];out+="0123456789abcdef"[b&15];}return out;}
};
}
std::string Sha256(const std::string& bytes){Hasher h;return h.Start()&&h.Add(bytes.data(),bytes.size())?h.End():std::string();}
namespace detail {
bool Plain(const std::filesystem::path& p,bool directory) {
  DWORD attributes=GetFileAttributesW(p.c_str());
  return attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT)&&
      static_cast<bool>(attributes&FILE_ATTRIBUTE_DIRECTORY)==directory;
}
bool Missing(const std::filesystem::path& p) {
  if(GetFileAttributesW(p.c_str())!=INVALID_FILE_ATTRIBUTES)return false;
  auto e=GetLastError();return e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND;
}
bool Normalize(const std::filesystem::path& input,std::filesystem::path& output,std::string& error) {
  error.clear();auto text=input.wstring();
  if(text.empty()||text.size()>30000||!input.is_absolute()||text.rfind(L"\\\\",0)==0||text.find(L':',2)!=std::wstring::npos) {
    error="The updater requires an absolute local drive directory without device/UNC/stream syntax.";return false;
  }
  auto normalized=input.lexically_normal();
  if(normalized==normalized.root_path()||!Plain(normalized,true)){error="The update directory is missing or is not an ordinary directory.";return false;}
  for(auto p=normalized;!p.empty()&&p!=p.parent_path();p=p.parent_path())if(!Plain(p,true)) {
    error="A redirected update directory component was refused.";return false;
  }
  output=normalized;return true;
}
Handle Open(const std::filesystem::path& p,bool rename) {
  Handle f(CreateFileW(p.c_str(),GENERIC_READ|(rename?DELETE:0),FILE_SHARE_READ,nullptr,OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
  if(!f)return {};
  BY_HANDLE_FILE_INFORMATION i={};
  if(!GetFileInformationByHandle(f.h,&i)||(i.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||i.nNumberOfLinks!=1)return {};
  return f;
}
bool Hash(HANDLE file,uint64_t& size,std::string& digest) {
  LARGE_INTEGER length={},zero={};size=0;digest.clear();
  if(!GetFileSizeEx(file,&length)||length.QuadPart<0||static_cast<uint64_t>(length.QuadPart)>2ULL*1024*1024*1024||
      !SetFilePointerEx(file,zero,nullptr,FILE_BEGIN))return false;
  Hasher hash;if(!hash.Start())return false;unsigned char buffer[65536];
  for(;;){DWORD read=0;if(!ReadFile(file,buffer,sizeof buffer,&read,nullptr))return false;if(!read)break;size+=read;if(size>static_cast<uint64_t>(length.QuadPart)||!hash.Add(buffer,read))return false;}
  digest=hash.End();return size==static_cast<uint64_t>(length.QuadPart)&&digest.size()==64;
}
bool Read(const std::filesystem::path& path,uint64_t limit,std::string& bytes) {
  bytes.clear();auto file=Open(path);if(!file)return false;LARGE_INTEGER size={};
  if(!GetFileSizeEx(file.h,&size)||size.QuadPart<0||static_cast<uint64_t>(size.QuadPart)>limit||size.QuadPart>16*1024*1024)return false;
  bytes.resize(static_cast<size_t>(size.QuadPart));size_t offset=0;
  while(offset<bytes.size()){DWORD read=0;if(!ReadFile(file.h,bytes.data()+offset,static_cast<DWORD>(std::min<size_t>(65536,bytes.size()-offset)),&read,nullptr)||!read){bytes.clear();return false;}offset+=read;}return true;
}
bool Write(const std::filesystem::path& path,const std::string& bytes,bool replace) {
  if(!Missing(path)&&(!replace||!Plain(path,false)))return false;
  auto temp=replace?path.parent_path()/(L".turan-write-"+std::filesystem::path(RandomId()).wstring()):path;
  Handle file(CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
  if(!file)return false;size_t offset=0;bool ok=true;
  while(offset<bytes.size()){DWORD written=0;if(!WriteFile(file.h,bytes.data()+offset,static_cast<DWORD>(std::min<size_t>(65536,bytes.size()-offset)),&written,nullptr)||!written){ok=false;break;}offset+=written;}
  if(ok)ok=FlushFileBuffers(file.h)!=FALSE;file.Close();
  if(ok&&replace)ok=(Missing(path)||Plain(path,false))&&MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
  if(!ok)DeleteFileW(temp.c_str());return ok;
}
bool Rename(HANDLE file,const std::filesystem::path& destination) {
  auto text=destination.wstring();size_t bytes=text.size()*sizeof(wchar_t);
  if(bytes>60000)return false;std::vector<unsigned char> storage(sizeof(FILE_RENAME_INFO)+bytes);
  auto info=reinterpret_cast<FILE_RENAME_INFO*>(storage.data());info->ReplaceIfExists=FALSE;info->RootDirectory=nullptr;
  info->FileNameLength=static_cast<DWORD>(bytes);memcpy(info->FileName,text.data(),bytes);
  return SetFileInformationByHandle(file,FileRenameInfo,info,static_cast<DWORD>(storage.size()))!=FALSE;
}
bool Match(const std::filesystem::path& path,const File& file) {
  auto handle=Open(path);uint64_t size=0;std::string sha;
  return handle&&Hash(handle.h,size,sha)&&size==file.bytes&&sha==file.sha256;
}
bool Parents(const std::filesystem::path& path,std::vector<Handle>& guards,bool create) {
  std::vector<std::filesystem::path> paths;
  for(auto p=path;!p.empty()&&p!=p.parent_path();p=p.parent_path())paths.push_back(p);
  std::reverse(paths.begin(),paths.end());
  for(const auto& p:paths) {
    if(Missing(p)&&create&&!CreateDirectoryW(p.c_str(),nullptr))return false;
    Handle guard(CreateFileW(p.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    BY_HANDLE_FILE_INFORMATION i={};
    if(!guard||!GetFileInformationByHandle(guard.h,&i)||(i.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||!(i.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))return false;
    guards.push_back(std::move(guard));
  }
  return true;
}
std::string RootId(const std::filesystem::path& root) {
  auto text=root.wstring();if(!text.empty())CharLowerBuffW(text.data(),static_cast<DWORD>(text.size()));
  return Sha256(std::string(reinterpret_cast<const char*>(text.data()),text.size()*sizeof(wchar_t)));
}
std::string RandomId() {
  unsigned char random[16];if(BCryptGenRandom(nullptr,random,sizeof random,BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)return {};
  std::string out;for(auto b:random){out+="0123456789abcdef"[b>>4];out+="0123456789abcdef"[b&15];}return out;
}
uint64_t Now() {
  FILETIME f;GetSystemTimeAsFileTime(&f);ULARGE_INTEGER n;n.LowPart=f.dwLowDateTime;n.HighPart=f.dwHighDateTime;
  return n.QuadPart/10000000ULL-11644473600ULL;
}
}
}
