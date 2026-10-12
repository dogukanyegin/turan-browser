#include "security/vault.h"
#include "core/state_file.h"
#include <windows.h>
#include <iostream>
#include <fstream>
#include <atomic>
#include <thread>
#define CHECK(x) do{if(!(x)){std::cerr<<"FAILED "<<__LINE__<<": "<<#x<<'\n';return 1;}}while(false)
int main(){using namespace turan;
 wchar_t temp[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temp));auto root=std::filesystem::path(temp)/(L"turan-vault-unit-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
 CHECK(CreateDirectoryW(root.c_str(),nullptr));auto second=root/L"second";CHECK(CreateDirectoryW(second.c_str(),nullptr));
 Secret pass("unit test master phrase 2026"),wrong("wrong master phrase 2026"),password("synthetic not a real user password"),updated("synthetic updated password"),export_pass("separate export master phrase 2026");std::string error;
 CHECK(!PasswordVault::PassphraseValid(Secret("short")));CHECK(!PasswordVault::PassphraseValid(Secret("Türkçeşifre")));CHECK(!PasswordVault::PassphraseValid(Secret("密碼密碼密碼密碼密碼密碼密碼")));CHECK(PasswordVault::PassphraseValid(Secret("密碼密碼密碼密碼密碼密碼密碼密")));
 CHECK(!PasswordVault::OriginValid("http://example.org"));CHECK(!PasswordVault::OriginValid("https://example.org/path"));CHECK(!PasswordVault::OriginValid("https://user@example.org"));
 for(auto bad:{"https://","https://example.org:0","https://example.org:99999","https://example.org\\@evil.example","https://example.org ","https://example.org?secret"})CHECK(!PasswordVault::OriginValid(bad));
 PasswordVault vault(root);CHECK(!vault.Exists());CHECK(!vault.Unlocked());CHECK(!vault.Create(Secret("short"),error));CHECK(vault.Create(pass,error));CHECK(vault.Exists());CHECK(vault.Unlocked());CHECK(!vault.Create(pass,error));
 CHECK(!vault.Save("http://example.org","user",password,error));CHECK(vault.Save("https://example.org","fixtureuser",password,error));CHECK(vault.Save("https://other.example","other",password,error));
 auto entries=vault.List("fixtureuser");CHECK(entries.size()==1);auto id=entries[0].id;CHECK(vault.List().size()==2);CHECK(vault.Reveal(id,"https://evil.example",error).empty());CHECK(vault.Reveal(id,"https://example.org",error).view()==password.view());
 std::string disk;CHECK(ReadStateFile(root/L"password-vault.dat",disk,16*1024*1024)==StateRead::Read);CHECK(disk.find(password.view())==std::string::npos);CHECK(disk.find("fixtureuser")==std::string::npos);CHECK(disk.find("https://example.org")==std::string::npos);
 CHECK(vault.Save("https://example.org","fixtureuser",updated,error));CHECK(vault.List().size()==2);CHECK(vault.List("fixtureuser")[0].id==id);CHECK(vault.Reveal(id,"https://example.org",error).view()==updated.view());
 CHECK(vault.Reauthenticate(pass,error));CHECK(!vault.Reauthenticate(wrong,error));CHECK(vault.Unlocked());
 CHECK(vault.Export(root/L"export.tvex",export_pass,error));CHECK(!vault.Export(root/L"export.tvex",export_pass,error));std::string export_disk;CHECK(ReadStateFile(root/L"export.tvex",export_disk,16*1024*1024)==StateRead::Read);CHECK(export_disk.find(updated.view())==std::string::npos);CHECK(export_disk.find("fixtureuser")==std::string::npos);
 PasswordVault other(second);CHECK(other.Create(pass,error));size_t imported=9;CHECK(!other.Import(root/L"export.tvex",wrong,error,imported));CHECK(imported==0&&other.List().empty());CHECK(other.Import(root/L"export.tvex",export_pass,error,imported));CHECK(imported==2&&other.List().size()==2);CHECK(other.Import(root/L"export.tvex",export_pass,error,imported));CHECK(other.List().size()==2);auto other_id=other.List("fixtureuser")[0].id;CHECK(other.Reveal(other_id,"https://example.org",error).view()==updated.view());
 CHECK(vault.ChangePassphrase(wrong,error));vault.Lock();CHECK(!vault.Unlocked()&&vault.List().empty());CHECK(vault.Reveal(id,"https://example.org",error).empty());CHECK(!vault.Unlock(pass,error));CHECK(vault.Unlock(wrong,error));CHECK(vault.Remove(id,error));CHECK(vault.List().size()==1);
 // Authentication failure and concurrent stale state cannot silently overwrite data.
 CHECK(ReadStateFile(root/L"password-vault.dat",disk,16*1024*1024)==StateRead::Read);auto damaged=disk;damaged.back()^=1;CHECK(WriteStateFile(root/L"password-vault.dat",damaged));CHECK(!vault.Save("https://new.example","new",password,error));vault.Lock();CHECK(!vault.Unlock(wrong,error));CHECK(WriteStateFile(root/L"password-vault.dat",disk));CHECK(vault.Unlock(wrong,error));
 auto bad_export=export_disk;bad_export.back()^=1;CHECK(WriteStateFile(root/L"bad.tvex",bad_export));CHECK(!other.Import(root/L"bad.tvex",export_pass,error,imported));CHECK(other.List().size()==2);
 // Two independently unlocked instances may race in one browser process. Only
 // one stale writer may commit; the other must preserve the winning file.
 PasswordVault concurrent(root);CHECK(concurrent.Unlock(wrong,error));std::atomic<int> ready{0};std::atomic<bool> go{false};bool first=false,second_write=false;std::string first_error,second_error;
 std::thread first_writer([&](){++ready;while(!go.load())std::this_thread::yield();first=vault.Save("https://race-first.example","first",password,first_error);});
 std::thread second_writer([&](){++ready;while(!go.load())std::this_thread::yield();second_write=concurrent.Save("https://race-second.example","second",password,second_error);});
 while(ready.load()!=2)std::this_thread::yield();go=true;first_writer.join();second_writer.join();CHECK(first!=second_write);concurrent.Lock();vault.Lock();CHECK(vault.Unlock(wrong,error));CHECK(vault.List().size()==2);CHECK(vault.List(first?"race-first":"race-second").size()==1);CHECK(GetFileAttributesW((root/L"password-vault.dat.lock").c_str())==INVALID_FILE_ATTRIBUTES);
 // A pre-existing arbitrary leaf cannot be consumed or deleted as our lease.
 CHECK(WriteStateFile(root/L"password-vault.dat.lock","unrelated retained leaf"));CHECK(!vault.Save("https://lock.example","lock",password,error));std::string retained;CHECK(ReadStateFile(root/L"password-vault.dat.lock",retained,1024)==StateRead::Read&&retained=="unrelated retained leaf");CHECK(DeleteFileW((root/L"password-vault.dat.lock").c_str()));
 for(int i=0;i<100;++i){auto generated=PasswordVault::Generate();CHECK(generated.size()==24&&SecretUtf8Valid(generated,128));}CHECK(PasswordVault::Generate(15).empty());
 // Exact fixture leaf removal only; arbitrary user directories are never traversed.
 vault.Lock();other.Lock();for(auto directory:{root,second})for(auto name:{L"password-vault.dat",L"password-vault.dat.bak"})CHECK(DeleteFileW((directory/name).c_str()));CHECK(DeleteFileW((root/L"export.tvex").c_str()));CHECK(DeleteFileW((root/L"bad.tvex").c_str()));CHECK(RemoveDirectoryW(second.c_str()));CHECK(RemoveDirectoryW(root.c_str()));std::cout<<"Password vault DPAPI/AES-GCM tests passed\n";
}
