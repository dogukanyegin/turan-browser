#include "core/profile_backup.h"
#include "core/model.h"
#include "core/state_file.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <map>
#include <regex>
#include <sstream>
#include <vector>

namespace turan {
namespace {
struct Handle {
  HANDLE value=INVALID_HANDLE_VALUE;
  ~Handle(){Close();}
  void Close(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=INVALID_HANDLE_VALUE;}
};
bool Plain(const std::filesystem::path& path,bool directory) {
  DWORD attributes=GetFileAttributesW(path.c_str());
  return attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT)&&
      static_cast<bool>(attributes&FILE_ATTRIBUTE_DIRECTORY)==directory;
}
bool Missing(const std::filesystem::path& path) {
  if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES)return false;
  return GetLastError()==ERROR_FILE_NOT_FOUND||GetLastError()==ERROR_PATH_NOT_FOUND;
}
std::wstring Fold(std::wstring text){if(!text.empty())CharLowerBuffW(text.data(),static_cast<DWORD>(text.size()));return text;}
bool Normalize(const std::filesystem::path& input,std::filesystem::path& output) {
  std::error_code error;
  auto absolute=std::filesystem::absolute(input,error).lexically_normal();
  if(error||absolute==absolute.root_path()||absolute.wstring().rfind(L"\\\\",0)==0)return false;
  // Refuse filesystem redirections on every existing component before resolving.
  for(auto path=absolute;!path.empty()&&path!=path.parent_path();path=path.parent_path()) {
    DWORD attributes=GetFileAttributesW(path.c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
  }
  output=std::filesystem::weakly_canonical(absolute,error);return !error;
}
bool Same(const std::filesystem::path& a,const std::filesystem::path& b){return Fold(a.wstring())==Fold(b.wstring());}
bool Inside(const std::filesystem::path& a,const std::filesystem::path& b) {
  auto child=Fold(a.wstring()),parent=Fold(b.wstring());
  if(child==parent)return true;
  if(parent.back()!=L'\\')parent+=L'\\';return child.rfind(parent,0)==0;
}
bool Overlap(const std::filesystem::path& a,const std::filesystem::path& b){return Inside(a,b)||Inside(b,a);}
bool ProfileShape(const std::filesystem::path& root) {
  return Plain(root,true)&&(Plain(root/L"browser-state.txt",false)||Plain(root/L"turan-engine.txt",false)||Plain(root/L"turan-engine-attempt.txt",false));
}
std::wstring Stamp() {
  FILETIME time;GetSystemTimeAsFileTime(&time);ULARGE_INTEGER value;value.LowPart=time.dwLowDateTime;value.HighPart=time.dwHighDateTime;
  return std::to_wstring(value.QuadPart)+L"-"+std::to_wstring(GetCurrentProcessId());
}
bool EngineNumbers(const std::string& engine,std::array<unsigned,7>& numbers) {
  static const std::regex pattern(R"(^([0-9]{1,4})\.([0-9]{1,4})\.([0-9]{1,6})\+g[0-9a-f]{4,40}\+chromium-([0-9]{1,4})\.([0-9]{1,4})\.([0-9]{1,6})\.([0-9]{1,6})$)");
  std::smatch match;if(!std::regex_match(engine,match,pattern))return false;
  for(size_t i=0;i<numbers.size();++i)numbers[i]=static_cast<unsigned>(std::stoul(match[i+1].str()));return true;
}
bool OlderEngine(const std::array<unsigned,7>& a,const std::array<unsigned,7>& b) {
  if(!std::equal(a.begin()+3,a.end(),b.begin()+3))return std::lexicographical_compare(a.begin()+3,a.end(),b.begin()+3,b.end());
  return std::lexicographical_compare(a.begin(),a.begin()+3,b.begin(),b.begin()+3);
}
bool ReadEngineMarker(const std::filesystem::path& file,std::string& engine,std::string& error) {
  std::string bytes;auto state=ReadStateFile(file,bytes,4096);
  if(state==StateRead::Missing){engine="unknown";return true;}
  std::istringstream in(bytes);std::string header,extra;unsigned schema=0;std::array<unsigned,7> numbers;
  if(state!=StateRead::Read||bytes.find('\0')!=std::string::npos||!(in>>header>>schema>>std::quoted(engine))||header!="TURAN_ENGINE"||schema!=1||
     !EngineNumbers(engine,numbers)||(in>>extra)){error="A profile engine/attempt marker is invalid; files were preserved.";return false;}return true;
}
bool SafeRelative(const std::string& name) {
  if(name.empty()||name.size()>4096||name.front()=='/'||name.back()=='/'||name.find_first_of("\\:")!=std::string::npos)return false;
  for(unsigned char c:name)if(c<32||c==127)return false;
  auto wide=Wide(name);if(wide.empty()||Utf8(wide)!=name)return false;
  std::istringstream parts(name);std::string part;
  while(std::getline(parts,part,'/')) {
    if(part.empty()||part=="."||part==".."||part.back()=='.'||part.back()==' ')return false;
    auto stem=Fold(Wide(part.substr(0,part.find('.'))));
    if(stem==L"con"||stem==L"prn"||stem==L"aux"||stem==L"nul"||
       (stem.size()==4&&(stem.rfind(L"com",0)==0||stem.rfind(L"lpt",0)==0)&&stem.back()>=L'1'&&stem.back()<=L'9'))return false;
  }
  return true;
}
bool Transient(const std::filesystem::path& relative) {
  auto name=Fold(relative.wstring());return name==L"lockfile"||name==L".turan-profile.lock";
}
struct Record {bool directory=false;uint64_t size=0;std::string sha,name;};
using Records=std::map<std::wstring,Record>;
bool SingleStream(const std::filesystem::path& path) {
  WIN32_FIND_STREAM_DATA stream={};
  HANDLE find=FindFirstStreamW(path.c_str(),FindStreamInfoStandard,&stream,0);
  if(find==INVALID_HANDLE_VALUE) {
    auto error=GetLastError();if(error==ERROR_HANDLE_EOF)return true;
    if(error!=ERROR_INVALID_FUNCTION&&error!=ERROR_INVALID_PARAMETER)return false;
    wchar_t volume[32768];DWORD flags=0;
    return GetVolumePathNameW(path.c_str(),volume,32768)&&
        GetVolumeInformationW(volume,nullptr,0,nullptr,nullptr,&flags,nullptr,0)&&!(flags&FILE_NAMED_STREAMS);
  }
  bool single=true;
  do {if(wcscmp(stream.cStreamName,L"::$DATA")!=0){single=false;break;}}
  while(FindNextStreamW(find,&stream));
  if(single&&GetLastError()!=ERROR_HANDLE_EOF)single=false;
  FindClose(find);return single;
}
bool Inventory(const std::filesystem::path& root,Records& records,ProfileLimits limits,uint64_t& bytes) {
  records.clear();bytes=0;std::error_code error;
  if(GetFileAttributesW(root.c_str())&FILE_ATTRIBUTE_ENCRYPTED||!SingleStream(root))return false;
  std::filesystem::recursive_directory_iterator iterator(root,error),end;if(error)return false;
  while(iterator!=end) {
    auto relative=iterator->path().lexically_relative(root);auto name=Utf8(relative.generic_wstring());
    if(iterator.depth()>=static_cast<int>(limits.depth)||!SafeRelative(name))return false;
    if(!Transient(relative)) {
      DWORD attributes=GetFileAttributesW(iterator->path().c_str());
      if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_ENCRYPTED))||!SingleStream(iterator->path()))return false;
      Record record;record.name=name;record.directory=(attributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
      if(!record.directory) {
        record.size=std::filesystem::file_size(iterator->path(),error);
        if(error||record.size>limits.bytes||bytes>limits.bytes-record.size)return false;bytes+=record.size;
      }
      if(!records.emplace(Fold(Wide(name)),record).second||records.size()>limits.entries)return false;
    }
    iterator.increment(error);if(error)return false;
  }
  return true;
}
class Hash {
 public:
  Hash() {
    if(BCryptOpenAlgorithmProvider(&algorithm_,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return;
    DWORD length=0,result=0;
    if(BCryptGetProperty(algorithm_,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&length),sizeof(length),&result,0)<0)return;
    object_.resize(length);
    if(BCryptCreateHash(algorithm_,&hash_,object_.data(),length,nullptr,0,0)<0)hash_=nullptr;
  }
  ~Hash(){if(hash_)BCryptDestroyHash(hash_);if(algorithm_)BCryptCloseAlgorithmProvider(algorithm_,0);}
  bool Add(unsigned char* data,DWORD size){return hash_&&BCryptHashData(hash_,data,size,0)>=0;}
  bool Finish(std::string& result) {
    unsigned char bytes[32];if(!hash_||BCryptFinishHash(hash_,bytes,sizeof(bytes),0)<0)return false;
    std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto byte:bytes)out<<std::setw(2)<<static_cast<unsigned>(byte);result=out.str();return true;
  }
 private:BCRYPT_ALG_HANDLE algorithm_=nullptr;BCRYPT_HASH_HANDLE hash_=nullptr;std::vector<unsigned char> object_;
};
bool Transfer(const std::filesystem::path& source,const std::filesystem::path& target,uint64_t size,std::string& digest,
              uint64_t& copied,uint64_t total,const ProfileProgress& progress) {
  Handle input,output;
  input.value=CreateFileW(source.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
  BY_HANDLE_FILE_INFORMATION information={};LARGE_INTEGER length;
  if(input.value==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(input.value,&information)||
     information.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_ENCRYPTED)||
     !GetFileSizeEx(input.value,&length)||length.QuadPart<0||static_cast<uint64_t>(length.QuadPart)!=size)return false;
  if(!target.empty()) {
    output.value=CreateFileW(target.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(output.value==INVALID_HANDLE_VALUE)return false;
  }
  Hash hash;std::array<unsigned char,65536> buffer;uint64_t remaining=size;
  while(remaining) {
    DWORD count=static_cast<DWORD>(std::min<uint64_t>(buffer.size(),remaining)),read=0;
    if(!ReadFile(input.value,buffer.data(),count,&read,nullptr)||read!=count||!hash.Add(buffer.data(),read))return false;
    if(output.value!=INVALID_HANDLE_VALUE){DWORD written=0;if(!WriteFile(output.value,buffer.data(),read,&written,nullptr)||written!=read)return false;}
    remaining-=read;copied+=read;if(progress&&!progress(copied,total))return false;
  }
  if(output.value!=INVALID_HANDLE_VALUE&&(!SetFileTime(output.value,&information.ftCreationTime,&information.ftLastAccessTime,&information.ftLastWriteTime)||!FlushFileBuffers(output.value)))return false;
  return hash.Finish(digest);
}
bool SameRecords(const Records& a,const Records& b) {
  if(a.size()!=b.size())return false;
  for(auto& [key,record]:a){auto found=b.find(key);if(found==b.end()||found->second.directory!=record.directory||found->second.size!=record.size)return false;}return true;
}
bool ReadManifest(const std::filesystem::path& backup,Records& records,ProfileBackupInfo& info,ProfileLimits limits) {
  std::string bytes;if(ReadStateFile(backup/L"backup-manifest.txt",bytes,32*1024*1024)!=StateRead::Read||bytes.find('\0')!=std::string::npos)return false;
  std::istringstream in(bytes);std::string header,kind;unsigned schema=0;
  if(!(in>>header>>schema>>info.created>>std::quoted(info.engine))||header!="TURAN_PROFILE_BACKUP"||schema!=1||!info.created||info.created>253402300799ULL)return false;
  std::array<unsigned,7> engine;if(info.engine!="unknown"&&!EngineNumbers(info.engine,engine))return false;
  info.bytes=info.files=0;records.clear();
  while(in>>kind) {
    Record record;record.directory=kind=="D";
    if(kind!="D"&&kind!="F")return false;
    if(!record.directory) {
      if(!(in>>record.size>>record.sha)||record.sha.size()!=64||record.size>limits.bytes||info.bytes>limits.bytes-record.size)return false;
      for(char c:record.sha)if(!(c>='0'&&c<='9')&&!(c>='a'&&c<='f'))return false;
      info.bytes+=record.size;++info.files;
    }
    if(!(in>>std::quoted(record.name))||!SafeRelative(record.name)||Transient(Wide(record.name))||
       std::count(record.name.begin(),record.name.end(),'/')>=limits.depth)return false;
    if(!records.emplace(Fold(Wide(record.name)),record).second||records.size()>limits.entries)return false;
  }
  return in.eof();
}
bool VerifyData(const std::filesystem::path& data,Records& expected,ProfileLimits limits,const ProfileProgress& progress) {
  Records actual;uint64_t total=0,copied=0;
  if(!Plain(data,true)||!Inventory(data,actual,limits,total)||!SameRecords(expected,actual))return false;
  for(auto& [key,record]:expected)if(!record.directory) {
    if(progress&&!progress(copied,total))return false;
    std::string digest;if(!Transfer(data/Wide(record.name),{},record.size,digest,copied,total,progress)||digest!=record.sha)return false;
  }
  return true;
}
bool CopyRecords(const std::filesystem::path& source,const std::filesystem::path& target,Records& records,uint64_t total,const ProfileProgress& progress,bool enforce) {
  uint64_t copied=0;
  for(auto& [key,record]:records) {
    if(progress&&!progress(copied,total))return false;
    auto destination=target/Wide(record.name);std::error_code error;
    std::filesystem::create_directories(record.directory?destination:destination.parent_path(),error);if(error)return false;
    if(!record.directory) {
      std::string digest;if(!Transfer(source/Wide(record.name),destination,record.size,digest,copied,total,progress)||(enforce&&digest!=record.sha))return false;
      record.sha=digest;
    }
  }
  return true;
}
bool NativeLock(const std::filesystem::path& root,Handle& lock) {
  auto path=root/L"lockfile";
  if(!Missing(path)&&!Plain(path,false))return false;
  // Matches Chromium's Windows process-singleton lock access/share contract.
  lock.value=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_DELETE_ON_CLOSE|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  return lock.value!=INVALID_HANDLE_VALUE;
}
bool Space(const std::filesystem::path& parent,uint64_t size,size_t entries) {
  ULARGE_INTEGER free;
  return GetDiskFreeSpaceExW(parent.c_str(),&free,nullptr,nullptr)&&free.QuadPart>=size+static_cast<uint64_t>(entries)*65536+16*1024*1024;
}
ProfileOperation Failed(const std::string& error,const std::filesystem::path& partial={}) {
  ProfileOperation result;result.error=error;if(!partial.empty())result.error+=" Incomplete data retained: "+Utf8(partial.wstring());return result;
}
std::filesystem::path Transaction(const std::filesystem::path& root){return root.parent_path()/(root.filename().wstring()+L".restore-transaction.txt");}
bool TransactionLock(const std::filesystem::path& root,Handle& lock) {
  auto path=root.parent_path()/(root.filename().wstring()+L".restore-operation.lock");
  if(!Missing(path)&&!Plain(path,false))return false;
  lock.value=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_DELETE,nullptr,OPEN_ALWAYS,
      FILE_FLAG_DELETE_ON_CLOSE|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  return lock.value!=INVALID_HANDLE_VALUE;
}
bool TransactionName(const std::wstring& name,const std::wstring& prefix) {
  if(name.rfind(prefix,0)!=0||name.size()<=prefix.size())return false;
  for(auto c:name.substr(prefix.size()))if(c!=L'-'&&(c<L'0'||c>L'9'))return false;
  return true;
}
}
ProfileLease::~ProfileLease(){Release();}
void ProfileLease::Release(){if(handle_){CloseHandle(handle_);handle_=nullptr;}root_.clear();}
bool ProfileLease::Acquire(const std::filesystem::path& root,std::string& error,bool create) {
  Release();std::filesystem::path normalized;if(!Normalize(root,normalized)){error="Profile path is not a plain local directory.";return false;}
  std::error_code ec;if(create)std::filesystem::create_directories(normalized,ec);
  if(ec||(!Plain(normalized,true)&&!(Missing(normalized)&&!create))||!Plain(normalized.parent_path(),true)){error="Profile directory could not be opened.";return false;}
  auto path=normalized.parent_path()/(normalized.filename().wstring()+L".turan-profile.lock");
  if(!Missing(path)&&!Plain(path,false)){error="Profile lease is not a regular file.";return false;}
  auto handle=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_DELETE,nullptr,OPEN_ALWAYS,
                         FILE_ATTRIBUTE_HIDDEN|FILE_FLAG_DELETE_ON_CLOSE|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(handle==INVALID_HANDLE_VALUE){error="This profile is already in use or cannot be locked.";return false;}
  handle_=handle;root_=normalized;return true;
}
bool ProfileLease::Owns(const std::filesystem::path& root) const {std::filesystem::path normalized;return handle_&&Normalize(root,normalized)&&Same(root_,normalized);}
bool ReadProfileEngine(const std::filesystem::path& root,std::string& engine,std::string& error) {
  std::string attempt;if(!ReadEngineMarker(root/L"turan-engine.txt",engine,error)||!ReadEngineMarker(root/L"turan-engine-attempt.txt",attempt,error))return false;
  if(attempt!="unknown") {
    std::array<unsigned,7> current,trying;EngineNumbers(attempt,trying);
    if(engine=="unknown"||(EngineNumbers(engine,current)&&OlderEngine(current,trying)))engine=attempt;
  }
  return true;
}
bool BeginProfileEngine(const std::filesystem::path& root,const std::string& engine,ProfileLease& lease) {
  std::array<unsigned,7> current,previous;std::string recorded,error;
  if(!lease.Owns(root)||!EngineNumbers(engine,current)||!ReadProfileEngine(root,recorded,error)||
     (recorded!="unknown"&&(!EngineNumbers(recorded,previous)||OlderEngine(current,previous))))return false;
  std::ostringstream out;out<<"TURAN_ENGINE 1 "<<std::quoted(engine)<<'\n';return WriteStateFile(root/L"turan-engine-attempt.txt",out.str());
}
bool MarkProfileEngine(const std::filesystem::path& root,const std::string& engine) {
  std::array<unsigned,7> numbers;std::string attempt,error;
  if(!EngineNumbers(engine,numbers)||!ReadEngineMarker(root/L"turan-engine-attempt.txt",attempt,error)||(attempt!="unknown"&&attempt!=engine))return false;
  std::ostringstream out;out<<"TURAN_ENGINE 1 "<<std::quoted(engine)<<'\n';
  return WriteStateFile(root/L"turan-engine.txt",out.str())&&(attempt=="unknown"||DeleteFileW((root/L"turan-engine-attempt.txt").c_str()));
}
bool RecoverProfileRestore(const std::filesystem::path& input,std::string& error) {
  std::filesystem::path root;if(!Normalize(input,root)){error="Profile path is not a plain local directory.";return false;}
  auto journal=Transaction(root);std::string bytes;
  auto read=ReadStateFile(journal,bytes,16384);if(read==StateRead::Missing)return true;
  Handle operation;if(!TransactionLock(root,operation)){error="A profile restore is still running.";return false;}
  read=ReadStateFile(journal,bytes,16384);if(read==StateRead::Missing)return true;
  std::istringstream in(bytes);std::string header,stage_name,previous_name,extra;unsigned schema=0;
  if(read!=StateRead::Read||bytes.find('\0')!=std::string::npos||!(in>>header>>schema>>std::quoted(stage_name)>>std::quoted(previous_name))||
     header!="TURAN_PROFILE_RESTORE"||schema!=1||(in>>extra)||
     !TransactionName(Wide(stage_name),root.filename().wstring()+L".restore-staging-")||
     !TransactionName(Wide(previous_name),root.filename().wstring()+L".before-restore-")) {
    error="Profile restore journal failed validation. All directories were preserved.";return false;
  }
  auto stage=root.parent_path()/Wide(stage_name),previous=root.parent_path()/Wide(previous_name);
  bool exists=Plain(root,true),old=Plain(previous,true),staged=Plain(stage,true);
  if((!exists&&!Missing(root))||(!old&&!Missing(previous))||(!staged&&!Missing(stage))) {
    error="Profile restore paths contain an unexpected file or redirection.";return false;
  }
  if((exists&&old&&staged)||(!exists&&!old)) {
    error="Profile restore state is ambiguous. All copies were preserved for recovery.";return false;
  }
  auto retained=exists?root:previous;
  if(!ProfileShape(retained)){error="The retained directory is not a Turan profile. All copies were preserved.";return false;}
  ProfileLease lease;if(!lease.Acquire(root,error,false))return false;
  Handle native;if(!NativeLock(retained,native)){error="Close the browser before recovering its profile restore.";return false;}
  if(!exists) {
    Handle startup;startup.value=CreateMutexW(nullptr,FALSE,L"Local\\ChromeProcessSingletonStartup!");
    if(!startup.value||startup.value==INVALID_HANDLE_VALUE){error="Could not acquire profile recovery mutex.";return false;}
    auto wait=WaitForSingleObject(startup.value,5000);
    if(wait!=WAIT_OBJECT_0&&wait!=WAIT_ABANDONED){error="Chromium startup prevented profile recovery.";return false;}
    native.Close();bool recovered=MoveFileExW(previous.c_str(),root.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;
    ReleaseMutex(startup.value);
    if(!recovered){error="The preserved profile could not be returned to its original directory.";return false;}
  }
  if(!DeleteFileW(journal.c_str())){error="Recovered profile, but its restore journal could not be cleared.";return false;}
  return true;
}
std::filesystem::path NextProfileBackup(const std::filesystem::path& root) {
  return root.parent_path()/(root.filename().wstring()+L".turan-backups")/(L"snapshot-"+Stamp());
}
ProfileOperation VerifyProfileBackup(const std::filesystem::path& input,ProfileLimits limits,ProfileProgress progress) {
  std::filesystem::path backup;if(!Normalize(input,backup)||!Plain(backup,true))return Failed("Backup directory is not a plain local directory.");
  ProfileOperation result;Records records;
  if(!ReadManifest(backup,records,result.info,limits)||!ProfileShape(backup/L"profile")||!VerifyData(backup/L"profile",records,limits,progress))return Failed("Backup manifest, paths or SHA-256 hashes failed validation.");
  std::string recorded,error;if(!ReadProfileEngine(backup/L"profile",recorded,error)||recorded!=result.info.engine)return Failed("Backup engine marker and manifest disagree.");
  result.ok=true;result.backup=backup;return result;
}
ProfileOperation CreateProfileBackup(const std::filesystem::path& root_input,const std::filesystem::path& destination,ProfileLease& lease,ProfileLimits limits,ProfileProgress progress) {
  std::filesystem::path root,target;if(!Normalize(root_input,root)||!Normalize(destination,target)||Overlap(root,target)||!lease.Owns(root)||!ProfileShape(root))return Failed("Backup requires the exclusively owned Turan profile and a separate destination.");
  if(!Missing(target))return Failed("Backup destination already exists; it will not be overwritten.");
  Handle native;if(!NativeLock(root,native))return Failed("Close every browser using this profile before backing it up.");
  Records records;uint64_t total=0;ProfileOperation result;
  if(!ReadProfileEngine(root,result.info.engine,result.error))return result;
  if(!Inventory(root,records,limits,total))return Failed("Profile paths, file count, file access or backup size limit failed validation.");
  std::error_code error;std::filesystem::create_directories(target.parent_path(),error);
  if(error||!Space(target.parent_path(),total,records.size()))return Failed("Insufficient writable disk space for a full profile backup.");
  auto partial=target.parent_path()/(target.filename().wstring()+L".partial-"+Stamp());
  if(!CreateDirectoryW(partial.c_str(),nullptr))return Failed("Could not reserve a new backup directory.");
  auto data=partial/L"profile";if(!CreateDirectoryW(data.c_str(),nullptr)||!CopyRecords(root,data,records,total,progress,false))return Failed("Profile copy was interrupted or a file could not be read exclusively.",partial);
  Records after;uint64_t checked=0;
  if(!Inventory(root,after,limits,checked)||!SameRecords(records,after)||!VerifyData(root,records,limits,progress))return Failed("Profile changed while being copied.",partial);
  result.info.created=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
  std::ostringstream manifest;manifest<<"TURAN_PROFILE_BACKUP 1 "<<result.info.created<<' '<<std::quoted(result.info.engine)<<'\n';
  for(auto& [key,record]:records) {
    if(record.directory)manifest<<"D ";else {manifest<<"F "<<record.size<<' '<<record.sha<<' ';++result.info.files;}
    manifest<<std::quoted(record.name)<<'\n';
  }
  if(!WriteStateFile(partial/L"backup-manifest.txt",manifest.str())||!VerifyProfileBackup(partial,limits,progress).ok||
     !MoveFileExW(partial.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH))return Failed("Backup verification or final directory commit failed.",partial);
  result.ok=true;result.info.bytes=total;result.backup=target;return result;
}
ProfileOperation RestoreProfileBackup(const std::filesystem::path& input,const std::filesystem::path& source,ProfileLease& lease,ProfileLimits limits,ProfileProgress progress) {
  std::filesystem::path root,backup;if(!Normalize(input,root)||!Normalize(source,backup)||Overlap(root,backup)||!lease.Owns(root)||!ProfileShape(root))return Failed("Restore requires an exclusively owned Turan profile and a separate backup.");
  Handle operation;if(!TransactionLock(root,operation)||!Missing(Transaction(root)))return Failed("A previous restore must finish or be recovered first.");
  Handle native;if(!NativeLock(root,native))return Failed("Close every browser using this profile before restoring it.");
  auto verified=VerifyProfileBackup(backup,limits,progress);if(!verified.ok)return verified;
  Records records;ProfileBackupInfo info;if(!ReadManifest(backup,records,info,limits)||!Space(root.parent_path(),info.bytes,records.size()))return Failed("Restore manifest or free disk space could not be validated.");
  auto stage=root.parent_path()/(root.filename().wstring()+L".restore-staging-"+Stamp());
  if(!CreateDirectoryW(stage.c_str(),nullptr)||!CopyRecords(backup/L"profile",stage,records,info.bytes,progress,true)||!VerifyData(stage,records,limits,progress))return Failed("Restore staging copy failed; the current profile is unchanged.",stage);
  // Hold Chromium's short startup mutex only for the final directory swap. A
  // legacy CEF process cannot acquire its lockfile during the handoff gap.
  Handle startup;startup.value=CreateMutexW(nullptr,FALSE,L"Local\\ChromeProcessSingletonStartup!");
  if(startup.value==INVALID_HANDLE_VALUE||!startup.value)return Failed("Could not acquire the Chromium startup mutex.",stage);
  auto wait=WaitForSingleObject(startup.value,5000);
  if(wait!=WAIT_OBJECT_0&&wait!=WAIT_ABANDONED)return Failed("Another Chromium startup prevented the restore handoff.",stage);
  auto previous=root.parent_path()/(root.filename().wstring()+L".before-restore-"+Stamp());
  std::ostringstream journal;journal<<"TURAN_PROFILE_RESTORE 1 "<<std::quoted(Utf8(stage.filename().wstring()))<<' '<<std::quoted(Utf8(previous.filename().wstring()))<<'\n';
  if(!WriteStateFile(Transaction(root),journal.str())){ReleaseMutex(startup.value);return Failed("Restore journal could not be committed; the profile is unchanged.",stage);}
  native.Close();
  bool moved=MoveFileExW(root.c_str(),previous.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;
  bool installed=moved&&MoveFileExW(stage.c_str(),root.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;
  bool rolled_back=true;
  if(moved&&!installed)rolled_back=MoveFileExW(previous.c_str(),root.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;
  if(installed) {
    if(!DeleteFileW(Transaction(root).c_str()))verified.error="Profile restored; recovery journal cleanup is pending.";
  } else if(rolled_back)DeleteFileW(Transaction(root).c_str());
  ReleaseMutex(startup.value);
  if(!installed) {
    auto result=Failed(rolled_back?"Restore handoff failed; current profile was retained.":"Restore handoff failed; original profile is preserved in the previous directory.",stage);
    if(moved)result.previous=previous;return result;
  }
  verified.previous=previous;return verified;
}
ProfileOperation PrepareProfileEngine(const std::filesystem::path& root,const std::string& engine,ProfileLease& lease) {
  std::array<unsigned,7> current,old;ProfileOperation result;
  if(!lease.Owns(root)||!EngineNumbers(engine,current))return Failed("Engine preparation requires an exclusively owned profile and a valid engine version.");
  if(!ReadProfileEngine(root,result.info.engine,result.error))return result;
  if(result.info.engine==engine){result.ok=true;return result;}
  if(result.info.engine!="unknown") {
    if(!EngineNumbers(result.info.engine,old)||OlderEngine(current,old))return Failed("This profile was opened or attempted by a newer engine. Restore a matching backup before using an older engine.");
  }
  bool cache=Plain(root/L"Default",true)||Plain(root/L"Cache",true)||Plain(root/L"Local State",false);
  if(!cache&&result.info.engine=="unknown"){result.ok=true;return result;}
  return CreateProfileBackup(root,NextProfileBackup(root),lease);
}
}
