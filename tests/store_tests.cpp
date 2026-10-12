#include "core/model.h"
#include "core/state_file.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#define CHECK(x) do {if(!(x)){std::cerr<<"FAILED "<<__LINE__<<": "<<#x<<'\n';return 1;}}while(false)
int main() {
  using namespace turan;
  wchar_t temporary[MAX_PATH];GetTempPathW(MAX_PATH,temporary);
  auto root=std::filesystem::path(temporary)/(L"turan-store-test-"+std::to_wstring(GetCurrentProcessId()));
  std::filesystem::create_directories(root);
  auto primary=root/L"browser-state.txt",backup=root/L"browser-state.bak";
  const std::string legacy="S 1 1 0 0 1 1\nB \"Legacy Türkçe\\\" bookmark\" \"https://example.org/bookmark\"\nH \"Title with\nnewline\" \"https://example.org/history\"\nFUTURE option=keep\n";
  Store store(root);CHECK(store.Load()==StoreLoad::Fresh);
  CHECK(WriteStateFile(primary,legacy));CHECK(store.Load()==StoreLoad::Loaded);
  CHECK(store.bookmarks.size()==1&&store.history.size()==1&&store.settings.turkish);
  CHECK(store.Load()==StoreLoad::Loaded&&store.bookmarks.size()==1); // repeated load is idempotent
  auto download_path=Utf8((root/L"Türkçe download.txt").wstring());
  store.RememberDownload({"normal","https://example.org/download",download_path,33,1700000000,false},false);
  store.RememberDownload({"private","https://private.example/download",download_path,33,1700000000,false},true);
  store.RememberDownload({"unsafe","javascript:alert(1)",download_path,33,1700000000,false},false);
  store.RememberDownload({"relative","https://example.org/download","relative.txt",33,1700000000,false},false);
  CHECK(store.downloads.size()==1);
  CHECK(PermissionOriginValid("https://example.org")&&PermissionOriginValid("http://127.0.0.1:32145"));
  CHECK(PermissionOriginValid("http://[::1]:8080")&&!PermissionOriginValid("https://[:::]"));
  CHECK(!PermissionOriginValid("http://example.org")&&!PermissionOriginValid("https://user@example.org"));
  CHECK(!PermissionOriginValid("https://example.org/path")&&!PermissionOriginValid("https://example.org:99999"));
  CHECK(!PermissionDecision(store.permissions,"https://example.org","https://example.org",CameraAccess));
  CHECK(RememberPermission(store.permissions,"https://example.org","https://example.org",CameraAccess|MicrophoneAccess,true));
  CHECK(PermissionDecision(store.permissions,"https://example.org","https://example.org",CameraAccess|MicrophoneAccess)==true);
  CHECK(!PermissionDecision(store.permissions,"https://example.org","https://other.example",CameraAccess));
  CHECK(RememberPermission(store.permissions,"https://example.org","https://example.org",MicrophoneAccess,false));
  CHECK(PermissionDecision(store.permissions,"https://example.org","https://example.org",CameraAccess|MicrophoneAccess)==false);
  CHECK(!RememberPermission(store.permissions,"https://example.org","https://example.org",ScreenAccess,true));
  store.Visit({"New","https://example.org/new"},false);CHECK(store.Save());
  std::string bytes;CHECK(ReadStateFile(backup,bytes,16*1024*1024)==StateRead::Read&&bytes==legacy);
  CHECK(ReadStateFile(primary,bytes,16*1024*1024)==StateRead::Read&&bytes.find("FUTURE option=keep")!=std::string::npos);
  CHECK(store.Load()==StoreLoad::Loaded&&store.downloads.size()==1&&store.downloads[0].path==download_path&&store.downloads[0].bytes==33);
  CHECK(store.permissions.size()==2&&PermissionDecision(store.permissions,"https://example.org","https://example.org",MicrophoneAccess)==false);
  const std::string damaged="S 1 1 0 0 1 1\nH \"truncated";
  CHECK(WriteStateFile(primary,damaged));CHECK(store.Load()==StoreLoad::Recovered&&store.recovered());
  CHECK(store.history.size()==1&&store.bookmarks.size()==1&&store.Save());
  CHECK(ReadStateFile(backup,bytes,16*1024*1024)==StateRead::Read&&bytes==legacy);
  unsigned rejected=0;
  for(auto& entry:std::filesystem::directory_iterator(root))if(entry.path().filename().wstring().find(L"browser-state.txt.rejected-")==0) {
    ++rejected;CHECK(ReadStateFile(entry.path(),bytes,16*1024*1024)==StateRead::Read&&bytes==damaged);
  }
  CHECK(rejected==1);CHECK(store.Load()==StoreLoad::Loaded);
  CHECK(WriteStateFile(primary,damaged)&&WriteStateFile(backup,"S 2 1 0 0 1 1\n"));
  CHECK(store.Load()==StoreLoad::Invalid&&!store.Save());
  CHECK(ReadStateFile(primary,bytes,16*1024*1024)==StateRead::Read&&bytes==damaged);
  CHECK(WriteStateFile(primary,"S 1 1 0 0 1 1\nB \"unsafe\" \"javascript:alert(1)\"\n"));
  CHECK(store.Load()==StoreLoad::Invalid);
  CHECK(WriteStateFile(primary,"S 1 1 0 0 1 1\nS 0 0 0 0 0 0\n"));CHECK(store.Load()==StoreLoad::Invalid);
  CHECK(WriteStateFile(primary,std::string(16*1024*1024+1,'x')));CHECK(store.Load()==StoreLoad::Invalid);
  // A write lock makes validation fail instead of reading a changing/truncated file.
  CHECK(WriteStateFile(primary,legacy));
  HANDLE lock=CreateFileW(primary.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  CHECK(lock!=INVALID_HANDLE_VALUE);CHECK(store.Load()==StoreLoad::Invalid);CloseHandle(lock);
  CHECK(store.Load()==StoreLoad::Loaded&&store.Save());
  // Replacing a hard-link leaf must not modify the other name's bytes.
  auto linked=root/L"linked.txt",outside=root/L"outside.txt";
  CHECK(WriteStateFile(outside,"unchanged")&&CreateHardLinkW(linked.c_str(),outside.c_str(),nullptr));
  CHECK(WriteStateFile(linked,"replacement"));CHECK(ReadStateFile(outside,bytes,32)==StateRead::Read&&bytes=="unchanged");
  // Symbolic-link refusal is tested when Windows permits unprivileged symlink creation.
  auto symbolic=root/L"symbolic.txt";
  if(CreateSymbolicLinkW(symbolic.c_str(),outside.c_str(),SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
    CHECK(ReadStateFile(symbolic,bytes,32)==StateRead::Error);
    CHECK(!WriteStateFile(symbolic,"bad")&&!QuarantineStateFile(symbolic));
    CHECK(ReadStateFile(outside,bytes,32)==StateRead::Read&&bytes=="unchanged");
    std::cout<<"PASS symbolic-link leaf refusal\n";
  } else std::cout<<"SKIP symbolic-link test: Windows denied creation ("<<GetLastError()<<")\n";
  CHECK(std::filesystem::weakly_canonical(root).parent_path()==std::filesystem::weakly_canonical(std::filesystem::path(temporary)));
  std::error_code error;std::filesystem::remove_all(root,error);
  std::cout<<"PASS complete legacy parsing, backup recovery, rejected-file preservation, invalid/oversize rejection, future records, persistent/private download isolation, write-lock refusal, hard-link isolation\n";
}
