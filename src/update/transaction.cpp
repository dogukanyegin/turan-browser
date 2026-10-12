#include "update/internal.h"
#include <algorithm>
#include <charconv>
#include <set>
#include <sstream>

namespace turan::update {
namespace {
using namespace detail;
constexpr wchar_t journal_name[]=L".turan-update-transaction";
constexpr wchar_t receipt_manifest[]=L".turan-update-receipt.manifest";
constexpr wchar_t receipt_signature[]=L".turan-update-receipt.sig";
struct OldFile {File new_file,old_file;bool existed=false;};
struct Transaction {
  std::filesystem::path root,stage;
  Manifest manifest;
  std::string metadata,signature,plan_hash;
  std::vector<OldFile> files;
  std::vector<Handle> directories;
  Handle lock;
};
bool StageName(const std::filesystem::path& path) {
  auto name=path.filename().string();return name.size()==46&&name.rfind(".turan-update-",0)==0&&name.substr(14).find_first_not_of("0123456789abcdef")==std::string::npos;
}
bool Kind(const std::filesystem::path& root,const Manifest& manifest,std::string& error) {
  auto marker=root/L"portable.flag";
  if(!Missing(marker)&&!Plain(marker,false)){error="The portable marker is redirected or invalid.";return false;}
  bool portable=Plain(marker,false);
  if(portable!=(manifest.kind=="portable")){error="The update package does not match this portable/installed distribution.";return false;}return true;
}
bool Base(Transaction& tx,const std::filesystem::path& app,std::string& error,bool runtime_required=true) {
  if(!Normalize(app,tx.root,error)||(runtime_required&&(!Plain(tx.root/L"TuranBrowser.exe",false)||!Plain(tx.root/L"TuranBrowser.dll",false)))) {
    if(error.empty())error="The selected directory is not a Turan executable distribution.";return false;
  }
  if(!Parents(tx.root,tx.directories)){error="Could not guard the update directory components.";return false;}
  tx.lock=Handle(CreateFileW((tx.root/L".turan-update.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,
      FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_DELETE_ON_CLOSE,nullptr));
  BY_HANDLE_FILE_INFORMATION i={};
  if(!tx.lock||!GetFileInformationByHandle(tx.lock.h,&i)||(i.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||i.nNumberOfLinks!=1) {
    error="Another update is active, or its lock file is redirected.";return false;
  }
  return true;
}
bool StageIdentity(Transaction& tx,const std::filesystem::path& staging,std::string& error) {
  if(!Normalize(staging,tx.stage,error)||!StageName(tx.stage)||RootId(tx.root.parent_path())!=RootId(tx.stage.parent_path())) {
    error="The update staging directory is not an owned sibling of this distribution.";return false;
  }
  std::string id;
  if(!Parents(tx.stage,tx.directories)||!Read(tx.stage/L"owner.txt",128,id)||id!=RootId(tx.root)+"\n") {
    error="The staged update belongs to another distribution or is redirected.";return false;
  }
  return true;
}
bool LoadMetadata(Transaction& tx,const Policy& policy,std::string& error,bool recovery) {
  if(!Read(tx.stage/L"manifest.txt",1024*1024,tx.metadata)||!Read(tx.stage/L"manifest.sig",1024,tx.signature)||
     !Authenticate(policy,tx.metadata,tx.signature,tx.manifest,error,recovery)||!Kind(tx.root,tx.manifest,error)) {
    if(error.empty())error="The staged update metadata could not be authenticated.";return false;
  }
  return true;
}
std::vector<File> TransactionFiles(const Transaction& tx) {
  auto files=tx.manifest.files;
  files.push_back({".turan-update-receipt.manifest",Sha256(tx.metadata),static_cast<uint64_t>(tx.metadata.size())});
  files.push_back({".turan-update-receipt.sig",Sha256(tx.signature),static_cast<uint64_t>(tx.signature.size())});
  return files;
}
bool Current(Transaction& tx,const Policy& policy,std::string& error) {
  Manifest prior;bool present=false;
  if(!Receipt(tx.root,policy,prior,present,error)||!Fresh(policy,tx.manifest,present?prior.sequence:0,error))return false;
  if(present&&(!(prior.version<tx.manifest.version)||prior.kind!=tx.manifest.kind)) {
    error="The signed offer cannot replace the installed release with an older version or another package type.";return false;
  }
  return true;
}
bool Plan(Transaction& tx,std::string& error) {
  tx.files.clear();std::ostringstream out;out<<"TURAN-UPDATE-PLAN-1\n"<<RootId(tx.root)<<'\n'<<Sha256(tx.metadata)<<'\n';
  for(const auto& f:TransactionFiles(tx)) {
    auto target=tx.root/std::filesystem::path(f.path);
    if(!Parents(target.parent_path(),tx.directories,true)){error="A runtime destination directory is redirected or inaccessible.";return false;}
    OldFile old;old.new_file=f;old.old_file.path=f.path;
    if(!Missing(target)) {
      auto file=Open(target,true);
      if(!file||!Hash(file.h,old.old_file.bytes,old.old_file.sha256)){error="A runtime file is busy, redirected, hard-linked or unreadable. Close Turan before applying.";return false;}
      old.existed=true;
    }
    out<<f.path<<'\t'<<(old.existed?std::to_string(old.old_file.bytes):"missing")<<'\t'<<(old.existed?old.old_file.sha256:"-")<<'\n';
    tx.files.push_back(std::move(old));
  }
  auto bytes=out.str();tx.plan_hash=Sha256(bytes);
  if(!Write(tx.stage/L"rollback-plan.txt",bytes)){error="Could not persist the update rollback plan.";return false;}
  return true;
}
bool LoadPlan(Transaction& tx,std::string& error) {
  std::string bytes;if(!Read(tx.stage/L"rollback-plan.txt",2*1024*1024,bytes)||Sha256(bytes)!=tx.plan_hash) {
    error="The update rollback plan is missing or changed; all files were preserved.";return false;
  }
  std::istringstream in(bytes);std::string line;
  if(!std::getline(in,line)||line!="TURAN-UPDATE-PLAN-1"||!std::getline(in,line)||line!=RootId(tx.root)||
     !std::getline(in,line)||line!=Sha256(tx.metadata)){error="The update rollback plan belongs to another destination.";return false;}
  tx.files.clear();
  for(const auto& f:TransactionFiles(tx)) {
    if(!std::getline(in,line)){error="The update rollback plan is truncated.";return false;}
    auto first=line.find('\t'),second=line.find('\t',first==std::string::npos?line.size():first+1);
    if(first==std::string::npos||second==std::string::npos||line.substr(0,first)!=f.path){error="The update rollback inventory is invalid.";return false;}
    OldFile old;old.new_file=f;old.old_file.path=f.path;auto size=line.substr(first+1,second-first-1),sha=line.substr(second+1);
    if(size=="missing"&&sha=="-")old.existed=false;
    else {
      auto n=std::from_chars(size.data(),size.data()+size.size(),old.old_file.bytes);
      if(size.empty()||n.ec!=std::errc()||n.ptr!=size.data()+size.size()||old.old_file.bytes>2ULL*1024*1024*1024||
         sha.size()!=64||sha.find_first_not_of("0123456789abcdef")!=std::string::npos){error="The rollback file fingerprint is invalid.";return false;}
      old.existed=true;old.old_file.sha256=sha;
    }
    tx.files.push_back(std::move(old));
  }
  if(std::getline(in,line)){error="The rollback plan has unexpected entries.";return false;}return true;
}
bool Journal(Transaction& tx,const char* phase,std::string& error) {
  const auto name=tx.stage.filename().string();
  std::string bytes="TURAN-UPDATE-TRANSACTION-1\n"+RootId(tx.root)+"\n"+name+"\n"+Sha256(tx.metadata)+"\n"+tx.plan_hash+"\n"+phase+"\n";
  if(!Write(tx.root/journal_name,bytes,true)){error="The update journal could not be persisted.";return false;}return true;
}
bool LoadJournal(Transaction& tx,const Policy& policy,std::string& phase,std::string& error) {
  std::string bytes;if(!Read(tx.root/journal_name,1024,bytes)){error="The update journal is unreadable or redirected.";return false;}
  std::istringstream in(bytes);std::string header,id,name,manifest_hash,extra;
  if(!std::getline(in,header)||header!="TURAN-UPDATE-TRANSACTION-1"||!std::getline(in,id)||id!=RootId(tx.root)||
     !std::getline(in,name)||!StageName(std::filesystem::path(name))||!std::getline(in,manifest_hash)||manifest_hash.size()!=64||
     !std::getline(in,tx.plan_hash)||tx.plan_hash.size()!=64||!std::getline(in,phase)||(phase!="applying"&&phase!="committed")||std::getline(in,extra)) {
    error="The update journal is invalid; files were preserved.";return false;
  }
  return StageIdentity(tx,tx.root.parent_path()/std::filesystem::path(name),error)&&LoadMetadata(tx,policy,error,true)&&
      Sha256(tx.metadata)==manifest_hash&&LoadPlan(tx,error);
}
bool RemoveExact(const std::filesystem::path& path,const File& expected) {
  auto file=Open(path,true);uint64_t size=0;std::string sha;
  if(!file||!Hash(file.h,size,sha)||size!=expected.bytes||sha!=expected.sha256)return false;
  FILE_DISPOSITION_INFO disposition={TRUE};return SetFileInformationByHandle(file.h,FileDispositionInfo,&disposition,sizeof disposition)!=FALSE;
}
bool Rollback(Transaction& tx,std::string& error) {
  // Validate the complete rollback state before changing a single file.
  for(const auto& f:tx.files) {
    auto target=tx.root/std::filesystem::path(f.new_file.path),backup=tx.stage/L"backup"/std::filesystem::path(f.new_file.path);
    if(!Parents(target.parent_path(),tx.directories)||(!Missing(backup)&&!Parents(backup.parent_path(),tx.directories))) {
      error="A rollback path is redirected; files were preserved.";return false;
    }
    if(!Missing(backup)) {
      if(!f.existed||!Match(backup,f.old_file)||(!Missing(target)&&!Match(target,f.new_file))) {
        error="A rollback backup or partially installed file changed; files were preserved.";return false;
      }
    } else if(f.existed) {
      if(!Match(target,f.old_file)){error="An original runtime file is missing or changed; files were preserved.";return false;}
    } else if(!Missing(target)&&!Match(target,f.new_file)) {
      error="A newly installed runtime file changed; files were preserved.";return false;
    }
  }
  for(auto i=tx.files.rbegin();i!=tx.files.rend();++i) {
    auto target=tx.root/std::filesystem::path(i->new_file.path),backup=tx.stage/L"backup"/std::filesystem::path(i->new_file.path);
    if(!Missing(backup)) {
      if(!Missing(target)&&!RemoveExact(target,i->new_file)){error="A partially installed file is busy; rollback remains pending.";return false;}
      auto original=Open(backup,true);uint64_t size=0;std::string sha;
      if(!original||!Hash(original.h,size,sha)||size!=i->old_file.bytes||sha!=i->old_file.sha256||!Rename(original.h,target)) {
        error="Could not restore an original runtime file; rollback remains pending.";return false;
      }
    } else if(!i->existed&&!Missing(target)&&!RemoveExact(target,i->new_file)) {
      error="A newly installed file is busy; rollback remains pending.";return false;
    }
  }
  if(!DeleteFileW((tx.root/journal_name).c_str())){error="The rollback finished but its journal could not be cleared.";return false;}
  // Retain the failed staging/backup directory for diagnosis. Never recursively
  // delete app directories or profiles as part of update/recovery.
  return true;
}
bool VerifyStage(Transaction& tx,std::string& error) {
  for(const auto& f:tx.manifest.files) {
    auto path=tx.stage/L"payload"/std::filesystem::path(f.path);
    if(!Parents(path.parent_path(),tx.directories)||!Match(path,f)){error="The staged payload has an invalid SHA-256, length or filesystem path.";return false;}
  }
  // Unlisted files are never installed. Refuse them to catch poisoned staging.
  std::set<std::string> names;for(const auto& f:tx.manifest.files)names.insert(f.path);
  std::error_code ec;std::filesystem::recursive_directory_iterator it(tx.stage/L"payload",ec),end;
  if(ec){error="The staged payload cannot be enumerated.";return false;}
  for(;it!=end;it.increment(ec)) {
    if(ec){error="The staged payload enumeration failed.";return false;}
    auto attr=GetFileAttributesW(it->path().c_str());
    if(attr==INVALID_FILE_ATTRIBUTES||(attr&FILE_ATTRIBUTE_REPARSE_POINT)){error="A redirected staged entry was refused.";return false;}
    if(!(attr&FILE_ATTRIBUTE_DIRECTORY)) {
      auto relative=it->path().lexically_relative(tx.stage/L"payload").generic_string();
      if(!names.count(relative)){error="The staged payload contains an unlisted file.";return false;}
    }
  }
  return true;
}
bool ApplyImpl(const std::filesystem::path& app,const std::filesystem::path& staging,const Policy& policy,std::string& error,const InstallHook& hook) {
  Transaction tx;if(!Base(tx,app,error))return false;
  if(!Missing(tx.root/journal_name)){error="An interrupted update must be recovered before applying another one.";return false;}
  if(!StageIdentity(tx,staging,error)||!LoadMetadata(tx,policy,error,false)||!Current(tx,policy,error)||!VerifyStage(tx,error))return false;
  auto payload=tx.stage/L"payload";
  if(!Write(payload/receipt_manifest,tx.metadata)||!Write(payload/receipt_signature,tx.signature)||
     !Parents(tx.stage/L"backup",tx.directories,true)||!Plan(tx,error)||!Journal(tx,"applying",error)) {
    if(error.empty())error="Could not prepare the update transaction.";return false;
  }
  size_t step=0;
  for(const auto& f:tx.files) {
    auto target=tx.root/std::filesystem::path(f.new_file.path),source=payload/std::filesystem::path(f.new_file.path),backup=tx.stage/L"backup"/std::filesystem::path(f.new_file.path);
    if(f.existed&&f.new_file.bytes==f.old_file.bytes&&f.new_file.sha256==f.old_file.sha256)continue;
    bool ok=Parents(backup.parent_path(),tx.directories,true);
    if(ok&&f.existed) {
      auto old=Open(target,true);uint64_t size=0;std::string sha;
      ok=old&&Hash(old.h,size,sha)&&size==f.old_file.bytes&&sha==f.old_file.sha256&&Rename(old.h,backup);
      if(ok&&hook)ok=hook(++step);
    } else if(ok)ok=Missing(target);
    if(ok) {
      auto next=Open(source,true);uint64_t size=0;std::string sha;
      ok=next&&Hash(next.h,size,sha)&&size==f.new_file.bytes&&sha==f.new_file.sha256&&Rename(next.h,target);
    }
    if(ok&&hook)ok=hook(++step);
    if(!ok) {
      std::string rollback_error;
      if(Rollback(tx,rollback_error))error="The update replacement failed. The previous runtime was restored; profiles were preserved.";
      else error="The update failed and recovery is pending: "+rollback_error;
      return false;
    }
  }
  for(const auto& f:tx.files)if(!Match(tx.root/std::filesystem::path(f.new_file.path),f.new_file)) {
    std::string rollback_error;bool restored=Rollback(tx,rollback_error);
    error=restored?"The installed update verification failed; the previous runtime was restored.":"The installed update verification failed; recovery is pending: "+rollback_error;return false;
  }
  if(!Journal(tx,"committed",error)) {
    std::string rollback_error;bool restored=Rollback(tx,rollback_error);
    error=restored?"The update commit could not be persisted; the previous runtime was restored.":"The update commit failed; recovery is pending: "+rollback_error;return false;
  }
  // Keep the signed receipt and verified old binaries, but move the completed
  // journal out of the startup recovery path. Failure here is recoverable.
  auto archive=tx.stage/L"committed-journal.txt";
  auto journal=Open(tx.root/journal_name,true);
  if(!journal||!Rename(journal.h,archive)){error="The update committed successfully; final journal recovery is still required.";return false;}
  error.clear();return true;
}
}
bool Check(const std::filesystem::path& app,const Policy& policy,Offer& offer,std::string& error,const Cancelled& cancelled) {
  offer={};if(!Configured(policy)){error="No authenticated NorthByte update channel has been provisioned.";return false;}
  std::filesystem::path root;if(!Normalize(app,root,error))return false;
  if(!Missing(root/journal_name)){error="An interrupted update must be recovered first.";return false;}
  Manifest previous;bool present=false;
  if(!Receipt(root,policy,previous,present,error)||!DownloadBytes(policy.manifest_url,1024*1024,offer.manifest_bytes,error,cancelled)||
     !DownloadBytes(policy.manifest_url+".sig",1024,offer.signature,error,cancelled)||!Authenticate(policy,offer.manifest_bytes,offer.signature,offer.manifest,error)||
     !Kind(root,offer.manifest,error)||!Fresh(policy,offer.manifest,present?previous.sequence:0,error))return false;
  if(present&&!(previous.version<offer.manifest.version)){error="The update is older than the installed signed release.";return false;}return true;
}
bool Stage(const std::filesystem::path& app,const Policy& policy,const Offer& offer,std::filesystem::path& staging,std::string& error,const Fetch& fetch,const Cancelled& cancelled) {
  staging.clear();Transaction tx;if(!Base(tx,app,error)||!Missing(tx.root/journal_name)) {if(error.empty())error="An interrupted update must be recovered first.";return false;}
  if(cancelled&&cancelled()){error="The update operation was cancelled.";return false;}
  tx.metadata=offer.manifest_bytes;tx.signature=offer.signature;
  if(!Authenticate(policy,tx.metadata,tx.signature,tx.manifest,error)||!Kind(tx.root,tx.manifest,error)||!Current(tx,policy,error))return false;
  uint64_t bytes=0;for(const auto& f:tx.manifest.files)bytes+=f.bytes;
  ULARGE_INTEGER free={};if(!GetDiskFreeSpaceExW(tx.root.parent_path().c_str(),&free,nullptr,nullptr)||free.QuadPart<bytes+64*1024*1024) {
    error="Insufficient free space for the verified update stage.";return false;
  }
  auto id=RandomId();if(id.empty()){error="Could not generate the update staging identity.";return false;}
  tx.stage=tx.root.parent_path()/std::filesystem::path(".turan-update-"+id);
  if(!CreateDirectoryW(tx.stage.c_str(),nullptr)||!Parents(tx.stage/L"payload",tx.directories,true)||
     !Write(tx.stage/L"owner.txt",RootId(tx.root)+"\n")||!Write(tx.stage/L"manifest.txt",tx.metadata)||!Write(tx.stage/L"manifest.sig",tx.signature)) {
    error="Could not create the isolated authenticated update stage.";return false;
  }
  // On failure retain the bounded stage for diagnostics, without touching app
  // files. No extracted archive paths or unsigned executables are executed.
  for(const auto& f:tx.manifest.files) {
    if(cancelled&&cancelled()){error="The update preparation was cancelled; runtime/profile files were preserved.";return false;}
    auto path=tx.stage/L"payload"/std::filesystem::path(f.path);
    bool downloaded=Parents(path.parent_path(),tx.directories,true)&&
        (fetch?fetch(tx.manifest.base_url+f.path,path,f.bytes,error):Download(tx.manifest.base_url+f.path,path,f.bytes,error,cancelled));
    if(!downloaded||!Match(path,f)) {
      if(error.empty())error="The downloaded payload SHA-256/length did not match the authenticated inventory.";return false;
    }
  }
  if(!VerifyStage(tx,error))return false;staging=tx.stage;error.clear();return true;
}
bool Apply(const std::filesystem::path& app,const std::filesystem::path& staging,const Policy& policy,std::string& error) {return ApplyImpl(app,staging,policy,error,{});}
bool ApplyForTest(const std::filesystem::path& app,const std::filesystem::path& staging,const Policy& policy,std::string& error,const InstallHook& hook) {return ApplyImpl(app,staging,policy,error,hook);}
bool Recover(const std::filesystem::path& app,const Policy& policy,std::string& error) {
  error.clear();std::filesystem::path root;if(!Normalize(app,root,error))return false;if(Missing(root/journal_name))return true;
  Transaction tx;if(!Base(tx,root,error,false))return false;std::string phase;
  if(!LoadJournal(tx,policy,phase,error))return false;
  if(phase=="applying")return Rollback(tx,error);
  for(const auto& f:tx.files)if(!Parents((tx.root/std::filesystem::path(f.new_file.path)).parent_path(),tx.directories)||!Match(tx.root/std::filesystem::path(f.new_file.path),f.new_file)) {
    error="The committed update was modified; recovery stopped without changing files.";return false;
  }
  auto journal=Open(tx.root/journal_name,true);
  if(!journal||!Rename(journal.h,tx.stage/L"committed-journal.txt")){error="The completed update journal is busy or already archived.";return false;}
  return true;
}
}
