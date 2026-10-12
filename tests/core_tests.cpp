#include "core/model.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#define CHECK(x) do { if(!(x)) { std::cerr<<"FAILED "<<__LINE__<<": "<<#x<<'\n'; return 1; } } while(false)
int main() {
  using namespace turan;
  CHECK(Host("https://USER@Ads.Example.com:443/a")=="ads.example.com");
  CHECK(Host("http://[::1]:8080/x")=="[::1]");
  CHECK(!IsThirdParty("https://cdn.example.com/x","https://example.com"));
  CHECK(IsThirdParty("https://evil-example.com","https://example.com"));
  CHECK(Navigate("  example.org  ",false)=="https://example.org");
  CHECK(Navigate("hello world",false)=="https://duckduckgo.com/?q=hello%20world");
  CHECK(Navigate("http://example.org",true)=="https://example.org");
  CHECK(!AllowedNavigation("javascript:alert(1)"));
  CHECK(!AllowedNavigation("file:///C:/Windows/win.ini"));
  CHECK(Wide(Utf8(L"Türkçe ı İ"))==L"Türkçe ı İ");
  wchar_t tmp[MAX_PATH];GetTempPathW(MAX_PATH,tmp);
  auto root=std::filesystem::path(tmp)/(L"turan-test-"+std::to_wstring(GetCurrentProcessId()));
  Store store(root);store.Visit({"private","https://private.example"},true);CHECK(store.history.empty());
  store.Visit({"normal \"title\"","https://example.org"},false);store.Bookmark(store.history.back());
  store.settings.turkish=true;CHECK(store.Save());
  Store other(root);other.Load();CHECK(other.history.size()==1&&other.bookmarks.size()==1&&other.settings.turkish);
  other.Bookmark(other.bookmarks.front());CHECK(other.bookmarks.empty());
  std::ofstream(root/L"rules.txt")<<"ads.example\n";
  Shield shield;shield.LoadRules(root/L"rules.txt",std::filesystem::path(TURAN_SOURCE_DIR)/L"resources/public_suffix_list.dat");
  CHECK(shield.Block("https://sub.ads.example/1","https://site.example"));
  CHECK(!shield.Block("https://notads.example/1","https://site.example"));
  CHECK(!shield.Block("https://ads.example/1","https://ads.example"));
  shield.enabled=false;CHECK(!shield.Block("https://ads.example/1","https://site.example"));
  shield.enabled=true;
  Shield policy;policy.LoadRules(root/L"rules.txt",std::filesystem::path(TURAN_SOURCE_DIR)/L"resources/public_suffix_list.dat",root);
  CHECK(policy.Report().ok);policy.Register(1,false);policy.Register(2,true);std::string error;
  CHECK(policy.SaveCustom("||test.example^$xhr\nsite.example##.advert\n",error));
  CHECK(policy.Check(1,{"https://test.example/x","https://site.example",FilterType::Xhr}).blocked);
  CHECK(policy.Events(1).custom==1&&policy.Events(2).events.empty());
  CHECK(!policy.SaveCustom("||test.example^$unknown\n",error));
  CHECK(policy.Check(1,{"https://test.example/x","https://site.example",FilterType::Xhr}).blocked);
  CHECK(policy.SetSiteEnabled(2,"https://site.example",false,error));CHECK(policy.SiteEnabled(1,"https://site.example"));
  CHECK(!policy.SiteEnabled(2,"https://site.example"));CHECK(policy.Cosmetics(2,"https://site.example").empty());
  CHECK(policy.SetSiteEnabled(1,"https://site.example",false,error));
  Shield restart;restart.LoadRules(root/L"rules.txt",std::filesystem::path(TURAN_SOURCE_DIR)/L"resources/public_suffix_list.dat",root);restart.Register(3,false);restart.Register(4,true);
  CHECK(!restart.SiteEnabled(3,"https://site.example")&&restart.SiteEnabled(4,"https://site.example"));
  auto generation=policy.Generation(1);policy.Begin(1);CHECK(policy.Events(1).events.empty());
  CHECK(policy.Check(1,{"https://test.example/x","https://other.example",FilterType::Xhr},generation).blocked);
  CHECK(policy.Events(1).events.empty());policy.Forget(2);CHECK(policy.Events(2).events.empty());
  std::ofstream(root/L"shield-custom.txt",std::ios::binary)<<"invalid$unknown\n";
  Shield recovered;recovered.LoadRules(root/L"rules.txt",std::filesystem::path(TURAN_SOURCE_DIR)/L"resources/public_suffix_list.dat",root);
  CHECK(recovered.Report().ok&&!recovered.Report().error.empty());
  CHECK(std::filesystem::weakly_canonical(root).parent_path()==std::filesystem::weakly_canonical(std::filesystem::path(tmp)));
  std::error_code ec;std::filesystem::remove_all(root,ec);
  std::cout<<"PASS URL, UTF8, privacy isolation, persistence, shield domain boundaries\n";return 0;
}
