#include "core/profile_backup.h"
#include "core/state_file.h"
#include "core/model.h"
#include <windows.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#define CHECK(x) do {if(!(x)){std::cerr<<"FAILED "<<__LINE__<<": "<<#x<<'\n';return 1;}}while(false)
namespace {
const std::string old_engine="144.0.36+gdeadbeef+chromium-144.0.7559.264";
const std::string new_engine="154.0.34+g14c5a08+chromium-154.0.8037.98";
const std::string future_engine="155.0.1+gdeadbeef+chromium-155.0.8100.1";
const std::string state="S 1 1 0 0 1 1\nB \"Legacy\" \"https://example.org/\"\n";
bool Fixture(const std::filesystem::path& root) {
  std::filesystem::create_directories(root/L"Default"/L"empty");
  return turan::WriteStateFile(root/L"browser-state.txt",state)&&
      turan::WriteStateFile(root/L"Default"/L"Türkçe.bin",std::string("abc\0xyz",7))&&
      turan::WriteStateFile(root/L"Default"/L"abc.txt","abc")&&
      turan::WriteStateFile(root/L"Default"/L"zero.txt","");
}
std::string Read(const std::filesystem::path& path) {
  std::string bytes;if(turan::ReadStateFile(path,bytes,32*1024*1024)!=turan::StateRead::Read)return "READ-ERROR";return bytes;
}
bool Journal(const std::filesystem::path& root,const std::filesystem::path& stage,const std::filesystem::path& previous) {
  std::ostringstream text;text<<"TURAN_PROFILE_RESTORE 1 "<<std::quoted(turan::Utf8(stage.filename().wstring()))<<' '<<std::quoted(turan::Utf8(previous.filename().wstring()))<<'\n';
  return turan::WriteStateFile(root.parent_path()/(root.filename().wstring()+L".restore-transaction.txt"),text.str());
}
}
int main() {
  using namespace turan;
  wchar_t temporary[MAX_PATH];GetTempPathW(MAX_PATH,temporary);
  auto outer=std::filesystem::path(temporary)/(L"turan-profile-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
  CHECK(CreateDirectoryW(outer.c_str(),nullptr));
  auto root=outer/L"Profile",backup=outer/L"snapshot";CHECK(Fixture(root));
  ProfileLease lease,second;std::string error;
  CHECK(lease.Acquire(root,error)&&lease.Owns(root)&&!second.Acquire(root,error));
  CHECK(MarkProfileEngine(root,old_engine));
  auto saved=CreateProfileBackup(root,backup,lease);CHECK(saved.ok&&saved.info.files==5&&saved.info.engine==old_engine);
  CHECK(VerifyProfileBackup(backup).ok&&Read(backup/L"profile"/L"browser-state.txt")==state);
  CHECK(Read(backup/L"profile"/L"Default"/L"Türkçe.bin")==std::string("abc\0xyz",7));
  auto manifest=Read(backup/L"backup-manifest.txt");
  CHECK(manifest.find("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")!=std::string::npos);
  CHECK(manifest.find("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")!=std::string::npos);
  CHECK(std::filesystem::is_directory(backup/L"profile"/L"Default"/L"empty"));
  CHECK(!std::filesystem::exists(backup/L"profile"/L".turan-profile.lock"));
  CHECK(!CreateProfileBackup(root,backup,lease).ok&&!CreateProfileBackup(root,root/L"nested",lease).ok);
  // Chromium's live root lock must block both directions, including legacy builds.
  auto lockfile=root/L"lockfile";
  HANDLE live=CreateFileW(lockfile.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_FLAG_DELETE_ON_CLOSE,nullptr);
  CHECK(live!=INVALID_HANDLE_VALUE);
  CHECK(!CreateProfileBackup(root,outer/L"busy",lease).ok&&!RestoreProfileBackup(root,backup,lease).ok);CloseHandle(live);
  HANDLE writing=CreateFileW((root/L"Default"/L"abc.txt").c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
  CHECK(writing!=INVALID_HANDLE_VALUE&&!CreateProfileBackup(root,outer/L"writing",lease).ok);CloseHandle(writing);
  CHECK(!CreateProfileBackup(root,outer/L"small",lease,{1,100,64}).ok);
  CHECK(!CreateProfileBackup(root,outer/L"few",lease,{1024,1,64}).ok);
  CHECK(!CreateProfileBackup(root,outer/L"shallow",lease,{1024,100,1}).ok);
  CHECK(!CreateProfileBackup(root,outer/L"cancelled",lease,{},[](uint64_t,uint64_t){return false;}).ok);
  CHECK(!std::filesystem::exists(outer/L"cancelled"));
  bool changed=false;
  auto changing=CreateProfileBackup(root,outer/L"changing",lease,{},[&](uint64_t copied,uint64_t) {
    if(!changed&&copied==state.size())changed=WriteStateFile(root/L"browser-state.txt","changed after copy");
    return true;
  });
  CHECK(changed&&!changing.ok&&!std::filesystem::exists(outer/L"changing"));
  CHECK(WriteStateFile(root/L"browser-state.txt",state));
  bool raced=false;auto competing=outer/L"competing";
  auto raced_commit=CreateProfileBackup(root,competing,lease,{},[&](uint64_t,uint64_t) {
    if(!raced){raced=CreateDirectoryW(competing.c_str(),nullptr)!=FALSE;if(raced)raced=WriteStateFile(competing/L"user.txt","keep existing destination");}
    return true;
  });
  CHECK(raced&&!raced_commit.ok&&Read(competing/L"user.txt")=="keep existing destination");
  // Never silently drop a named NTFS stream (for example download origin data).
  auto alternate=root/L"Default"/L"abc.txt:Zone.Identifier";
  HANDLE stream=CreateFileW(alternate.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  CHECK(stream!=INVALID_HANDLE_VALUE);DWORD written=0;
  CHECK(WriteFile(stream,"[ZoneTransfer]\r\nZoneId=3\r\n",26,&written,nullptr));CloseHandle(stream);
  CHECK(!CreateProfileBackup(root,outer/L"streams",lease).ok&&DeleteFileW(alternate.c_str()));
  // Digest mismatch and unlisted files fail verification before any profile mutation.
  CHECK(WriteStateFile(backup/L"profile"/L"Default"/L"abc.txt","abd"));
  CHECK(!VerifyProfileBackup(backup).ok&&!RestoreProfileBackup(root,backup,lease).ok&&Read(root/L"browser-state.txt")==state);
  CHECK(WriteStateFile(backup/L"profile"/L"Default"/L"abc.txt","abc"));
  CHECK(WriteStateFile(backup/L"profile"/L"unexpected","extra")&&!VerifyProfileBackup(backup).ok);
  CHECK(DeleteFileW((backup/L"profile"/L"unexpected").c_str()));
  for(const std::string bad: {"../escape","Default/../escape","CON.txt","Default:stream","Default\\file"}) {
    CHECK(WriteStateFile(backup/L"backup-manifest.txt",manifest+"D \""+bad+"\"\n"));
    CHECK(!VerifyProfileBackup(backup).ok&&!RestoreProfileBackup(root,backup,lease).ok);
  }
  CHECK(WriteStateFile(backup/L"backup-manifest.txt",manifest+"D \"Default\"\n")&&!VerifyProfileBackup(backup).ok);
  CHECK(WriteStateFile(backup/L"backup-manifest.txt",manifest));
  auto symbolic=root/L"Default"/L"link";
  if(CreateSymbolicLinkW(symbolic.c_str(),(root/L"browser-state.txt").c_str(),SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
    CHECK(!CreateProfileBackup(root,outer/L"linked",lease).ok&&DeleteFileW(symbolic.c_str()));
    std::cout<<"PASS profile reparse-point refusal\n";
  } else std::cout<<"SKIP profile symlink creation: "<<GetLastError()<<'\n';
  CHECK(WriteStateFile(root/L"browser-state.txt","current user data")&&MarkProfileEngine(root,new_engine));
  auto restored=RestoreProfileBackup(root,backup,lease);
  if(!restored.ok)std::cerr<<restored.error<<'\n';
  CHECK(restored.ok&&lease.Owns(root)&&!second.Acquire(root,error));
  CHECK(Read(root/L"browser-state.txt")==state&&Read(restored.previous/L"browser-state.txt")=="current user data");
  std::string engine;CHECK(ReadProfileEngine(root,engine,error)&&engine==old_engine);
  auto upgrade=PrepareProfileEngine(root,new_engine,lease);CHECK(upgrade.ok&&!upgrade.backup.empty()&&VerifyProfileBackup(upgrade.backup).ok);
  CHECK(ReadProfileEngine(root,engine,error)&&engine==old_engine); // preparation never marks a successful engine launch
  CHECK(BeginProfileEngine(root,new_engine,lease)&&ReadProfileEngine(root,engine,error)&&engine==new_engine);
  CHECK(!PrepareProfileEngine(root,old_engine,lease).ok&&!BeginProfileEngine(root,old_engine,lease));
  CHECK(MarkProfileEngine(root,new_engine)&&PrepareProfileEngine(root,new_engine,lease).ok&&PrepareProfileEngine(root,new_engine,lease).backup.empty());
  CHECK(!std::filesystem::exists(root/L"turan-engine-attempt.txt"));
  CHECK(BeginProfileEngine(root,future_engine,lease)&&!PrepareProfileEngine(root,new_engine,lease).ok);
  CHECK(!MarkProfileEngine(root,new_engine));
  CHECK(MarkProfileEngine(root,future_engine));
  CHECK(MarkProfileEngine(root,future_engine)&&!PrepareProfileEngine(root,new_engine,lease).ok);
  CHECK(ReadProfileEngine(root,engine,error)&&engine==future_engine);
  CHECK(WriteStateFile(root/L"turan-engine.txt","broken")&&!PrepareProfileEngine(root,new_engine,lease).ok);
  CHECK(MarkProfileEngine(root,new_engine));lease.Release();
  // Recover each directory handoff crash state. Every retained copy stays intact.
  for(unsigned scenario=0;scenario<4;++scenario) {
    auto target=outer/(L"Recovery"+std::to_wstring(scenario));
    auto stage=outer/(target.filename().wstring()+L".restore-staging-123-1");
    auto previous=outer/(target.filename().wstring()+L".before-restore-123-1");
    CHECK(Fixture(target)&&Fixture(stage)&&Journal(target,stage,previous));
    if(scenario>0)CHECK(MoveFileExW(target.c_str(),previous.c_str(),MOVEFILE_WRITE_THROUGH));
    if(scenario>1)CHECK(MoveFileExW(stage.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH));
    if(scenario==3)CHECK(Fixture(stage)); // all three is ambiguous and must stop
    CHECK(RecoverProfileRestore(target,error)==(scenario!=3));
    CHECK(Read(target/L"browser-state.txt")==state);
    if(scenario==0||scenario==1||scenario==3)CHECK(std::filesystem::is_directory(stage));
    if(scenario==2||scenario==3)CHECK(Read(previous/L"browser-state.txt")==state);
  }
  auto unsafe=outer/L"Unsafe";CHECK(Fixture(unsafe));
  CHECK(WriteStateFile(outer/L"Unsafe.restore-transaction.txt","TURAN_PROFILE_RESTORE 1 \"../escape\" \"Unsafe.before-restore-123\"\n"));
  CHECK(!RecoverProfileRestore(unsafe,error)&&Read(unsafe/L"browser-state.txt")==state);
  auto unknown=outer/L"Unknown";CHECK(Fixture(unknown)&&lease.Acquire(unknown,error));
  CHECK(PrepareProfileEngine(unknown,new_engine,lease).ok);lease.Release();
  CHECK(std::filesystem::is_directory(outer/L"Unknown.turan-backups"));
  auto fresh=outer/L"Fresh";CHECK(lease.Acquire(fresh,error));
  CHECK(PrepareProfileEngine(fresh,new_engine,lease).ok&&!std::filesystem::exists(outer/L"Fresh.turan-backups"));lease.Release();
  // Cleanup is confined to this fresh process-owned temp directory.
  CHECK(std::filesystem::weakly_canonical(outer).parent_path()==std::filesystem::weakly_canonical(std::filesystem::path(temporary)));
  std::error_code cleanup;std::filesystem::remove_all(outer,cleanup);CHECK(!cleanup);
  std::cout<<"PASS full-profile SHA-256 snapshots, live/writer locks, bounds, cancellation, tamper/path rejection, restore rollback copy, exclusive lease handoff, crash recovery and engine downgrade guard\n";
}
