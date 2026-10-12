#include "privacy/tor_browser.h"
#include "core/model.h"
#include "core/state_file.h"
#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <fstream>
#define CHECK(x) do{if(!(x)){std::cerr<<"FAILED "<<__LINE__<<": "<<#x<<'\n';return 1;}}while(false)
int wmain(int argc,wchar_t** argv) {
  using namespace turan;
  if(argc==3&&std::wstring(argv[1])==L"--verify") {
    auto result=VerifyTorBrowser(argv[2]);std::cout<<(result.ok?"VERIFIED ":"REJECTED ")<<result.verified_files<<' '<<result.error<<'\n';return result.ok?0:2;
  }
  std::wstring target;std::string error;
  for(auto url:{"https://example.org/a?x=1&y=2#hash","http://example.onion/","http://[::1]:8080/","https://xn--bcher-kva.example/","https://example.org/encoded%20space%22quote"})
    CHECK(TorTargetUrl(url,target,error)&&Utf8(target)==url&&error.empty());
  for(auto url:{"","file:///c:/a","javascript:alert(1)","data:text/plain,hello","turan://settings","--profile evil","https://","https://user:secret@example.org/","http://@example.org/","http://example.org\\@evil.org/","https://example.org/\" -profile x","https://example.org/a b","https://example.org/\r\nfoo","http://example.org:99999/"})
    CHECK(!TorTargetUrl(url,target,error)&&target.empty()&&!error.empty());
  CHECK(!TorTargetUrl(std::string("https://example.org/\0secret",27),target,error));
  CHECK(!TorTargetUrl("https://example.org/"+std::string(17000,'a'),target,error));
  // Round-trip actual Windows argv parsing, including empty/trailing slash/quotes.
  for(auto arg:{L"",L"C:\\Türkçe space\\",L"https://example.org/?a=1&b=2",L"a\\\"b",L"--profile=bad",L"a\\\\\"",L"x\"y"}) {
    auto command=L"probe "+QuoteProcessArgument(arg);int count=0;auto args=CommandLineToArgvW(command.c_str(),&count);
    CHECK(args&&count==2&&std::wstring(args[1])==arg);LocalFree(args);
  }
  wchar_t tmp[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,tmp));
  auto root=std::filesystem::path(tmp)/(L"turan-tor-unit-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
  CHECK(CreateDirectoryW(root.c_str(),nullptr));auto profile=root/L"Profile",fake=root/L"Tor Browser";
  std::filesystem::create_directories(profile);std::filesystem::create_directories(fake/L"Browser");
  CHECK(WriteStateFile(fake/L"Browser"/L"firefox.exe","unsigned fake"));
  CHECK(TorBrowserPreference(profile).empty());CHECK(SaveTorBrowserPreference(profile,fake));CHECK(TorBrowserPreference(profile)==fake);
  CHECK(TorBrowserCandidates(profile).front()==fake);
  CHECK(!VerifyTorBrowser(fake).ok&&!OpenInTorBrowser(fake,"https://example.org/").ok);
  CHECK(!VerifyTorBrowser(std::filesystem::path(fake.wstring()+L"\\")).ok);
  CHECK(!OpenInTorBrowser(fake,"file:///c:/secret").ok);
  CHECK(!VerifyTorBrowser(L"\\\\server\\Tor Browser").ok&&!VerifyTorBrowser(L"relative").ok&&!VerifyTorBrowser(root/L"missing").ok);
  CHECK(!SaveTorBrowserPreference(profile,L"C:\\invalid:stream"));
  CHECK(WriteStateFile(profile/L"tor-browser-path.txt","TURAN_TOR_BROWSER 1 \"C:\\\\unknown\" junk\n"));CHECK(TorBrowserPreference(profile).empty());
  // Remove only exact fixture leaves created by this test; no recursive deletion.
  CHECK(DeleteFileW((profile/L"tor-browser-path.txt").c_str()));CHECK(DeleteFileW((fake/L"Browser"/L"firefox.exe").c_str()));
  CHECK(RemoveDirectoryW((fake/L"Browser").c_str()));CHECK(RemoveDirectoryW(fake.c_str()));CHECK(RemoveDirectoryW(profile.c_str()));CHECK(RemoveDirectoryW(root.c_str()));
  std::cout<<"PASS strict Tor URL/argv, preference, candidate detection, unsigned/missing/network refusal\n";return 0;
}
