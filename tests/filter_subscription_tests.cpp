#include "privacy/filter_subscription.h"
#include "core/state_file.h"
#include "core/model.h"
#include "ui/filter_subscription_window.h"
#include <windows.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#define CHECK(x) do {if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<'\n';return 1;}}while(false)
namespace {
void Wait(turan::FilterSubscriptions& subscriptions){for(int i=0;i<6000&&subscriptions.Busy();++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));}
std::string List(const char* domain){return std::string("[Adblock Plus 2.0]\n! synthetic test only\n||")+domain+"^$script\nsite.example##.advert\n";}
}
int wmain(int argc,wchar_t** argv){
  if(argc==3&&std::wstring(argv[2])==L"--live"){
    using namespace turan;auto suffix=std::make_shared<PublicSuffix>();std::string error;std::ifstream in(std::filesystem::path(argv[1])/L"resources/public_suffix_list.dat",std::ios::binary);std::ostringstream psl;psl<<in.rdbuf();CHECK(suffix->Load(psl.str(),error));
    wchar_t temporary[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temporary));auto profile=std::filesystem::path(temporary)/(L"TuranFilterSubscriptionLive-"+std::to_wstring(GetCurrentProcessId()));CHECK(std::filesystem::create_directory(profile));
    auto began=std::chrono::steady_clock::now();
    {
      FilterSubscriptions subscriptions(profile,suffix);for(const auto& d:FilterSubscriptions::Catalog())CHECK(subscriptions.SetEnabled(d.id,true,true,error));CHECK(subscriptions.RefreshAsync(true));Wait(subscriptions);CHECK(!subscriptions.Busy());
      for(const auto& status:subscriptions.Snapshot()){std::cout<<status.id<<" bytes="<<status.bytes<<" network="<<status.network<<" cosmetic="<<status.cosmetic<<" unsupported="<<status.unsupported<<" invalid="<<status.invalid<<" sha256="<<status.sha256<<" error="<<status.error<<'\n';CHECK(status.cached&&status.error.empty());}
      FilterReport report;auto engine=FilterEngine::Compile(subscriptions.Sources(),suffix,report);CHECK(engine&&report.ok);CHECK(engine->Check({"https://googleads.g.doubleclick.net/pagead/ads?client=fixture","https://site.example/",FilterType::Script}).blocked);CHECK(!engine->Check({"https://site.example/owned-static.js","https://site.example/",FilterType::Script}).blocked);
      Shield shield;shield.LoadRules(std::filesystem::path(argv[1])/L"resources/shield-domains.txt",std::filesystem::path(argv[1])/L"resources/public_suffix_list.dat",profile);CHECK(shield.SetSubscriptions(subscriptions.Sources(),error));CHECK(shield.Check(-1,{"https://googleads.g.doubleclick.net/pagead/ads?client=fixture","https://site.example/",FilterType::Script}).blocked);
    }
    {FilterSubscriptions restored(profile,suffix);CHECK(restored.Sources().size()==2);for(const auto& s:restored.Snapshot())CHECK(s.cached&&s.enabled&&s.error.empty());CHECK(!restored.RefreshAsync());}
    std::cout<<"PASS official live TLS download/full parser/combined engine/cache restart "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count()<<" seconds\n";
    for(const auto& entry:std::filesystem::directory_iterator(profile))CHECK(std::filesystem::remove(entry.path()));CHECK(std::filesystem::remove(profile));return 0;
  }
  using namespace turan;CHECK(argc==2);std::string error;auto suffix=std::make_shared<PublicSuffix>();std::ifstream in(std::filesystem::path(argv[1])/L"resources/public_suffix_list.dat",std::ios::binary);std::ostringstream text;text<<in.rdbuf();CHECK(suffix->Load(text.str(),error));
  wchar_t temporary[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temporary));auto profile=std::filesystem::path(temporary)/(L"TuranFilterSubscriptionTest-"+std::to_wstring(GetCurrentProcessId()));CHECK(std::filesystem::create_directory(profile));
  auto now=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
  std::atomic<unsigned> fetched{0};std::string next=List("ads.example");bool network_ok=true;
  auto fetch=[&](const std::string&,std::string& body,std::string& message){++fetched;if(!network_ok){message="fixture network unavailable";return false;}body=next;return true;};
  {
    FilterSubscriptions subscriptions(profile,suffix,fetch);CHECK(!subscriptions.Snapshot()[0].enabled);CHECK(subscriptions.Sources().empty());CHECK(!subscriptions.RefreshAsync());CHECK(fetched==0);
    CHECK(!subscriptions.SetEnabled("easylist",true,false,error));CHECK(!subscriptions.SetEnabled("../../escape",true,true,error));CHECK(fetched==0);
    CHECK(subscriptions.SetEnabled("easylist",true,true,error));CHECK(subscriptions.RefreshAsync(false,now));Wait(subscriptions);CHECK(!subscriptions.Busy());CHECK(fetched==1);CHECK(subscriptions.Snapshot()[0].cached);CHECK(subscriptions.Snapshot()[0].network==1);CHECK(subscriptions.Snapshot()[0].cosmetic==1);CHECK(subscriptions.Snapshot()[0].sha256.size()==64);CHECK(!subscriptions.RefreshAsync(false,now+1));
    FilterReport report;auto engine=FilterEngine::Compile(subscriptions.Sources(),suffix,report);CHECK(engine&&engine->Check({"https://ads.example/a.js","https://site.example/",FilterType::Script}).blocked);CHECK(!engine->Check({"https://good.example/a.js","https://site.example/",FilterType::Script}).blocked);
    next=List("replacement.example");CHECK(subscriptions.RefreshAsync(true,now+2));Wait(subscriptions);CHECK(fetched==2);CHECK(subscriptions.Sources()[0].text==next);CHECK(subscriptions.Rollback("easylist",error));CHECK(subscriptions.Sources()[0].text==List("ads.example"));
    network_ok=false;CHECK(subscriptions.RefreshAsync(true,now+3));Wait(subscriptions);CHECK(subscriptions.Sources()[0].text==List("ads.example"));CHECK(subscriptions.Snapshot()[0].error=="fixture network unavailable");CHECK(!subscriptions.RefreshAsync(false,now+4));
    network_ok=true;next="<html>upstream error</html>";CHECK(subscriptions.RefreshAsync(true,now+4));Wait(subscriptions);CHECK(subscriptions.Sources()[0].text==List("ads.example"));CHECK(!subscriptions.Snapshot()[0].error.empty());
    next="[Adblock Plus 2.0]\n||allowed.example^$script\n||blocked.example^$redirect=noopjs\n";CHECK(subscriptions.RefreshAsync(true,now+5));Wait(subscriptions);CHECK(subscriptions.Snapshot()[0].unsupported==1);CHECK(subscriptions.Snapshot()[0].network==1);
    Shield shield;shield.LoadRules(std::filesystem::path(argv[1])/L"resources/shield-domains.txt",std::filesystem::path(argv[1])/L"resources/public_suffix_list.dat",profile);CHECK(shield.SetSubscriptions(subscriptions.Sources(),error));CHECK(shield.SaveCustom("||custom.example^$script\n",error));
    CHECK(shield.Check(-1,{"https://allowed.example/a.js","https://site.example/",FilterType::Script}).blocked);CHECK(shield.Check(-1,{"https://custom.example/a.js","https://site.example/",FilterType::Script}).blocked);CHECK(!shield.Check(-1,{"https://blocked.example/a.js","https://site.example/",FilterType::Script}).blocked);CHECK(!shield.SaveCustom("||custom.example^$unknown-option\n",error));
    CHECK(!shield.SetSubscriptions({{"Outside catalog",List("evil.example")}},error));CHECK(shield.Check(-1,{"https://allowed.example/a.js","https://site.example/",FilterType::Script}).blocked);CHECK(shield.SetSubscriptions({},error));CHECK(!shield.Check(-1,{"https://allowed.example/a.js","https://site.example/",FilterType::Script}).blocked);CHECK(shield.Check(-1,{"https://custom.example/a.js","https://site.example/",FilterType::Script}).blocked);
    next=std::string(5*1024*1024+1,'x');CHECK(subscriptions.RefreshAsync(true,now+6));Wait(subscriptions);CHECK(subscriptions.Snapshot()[0].bytes<5*1024*1024);CHECK(!subscriptions.Snapshot()[0].error.empty());
    next="[Adblock Plus 2.0]\n||only.example^$script\n||invalid..example^$unknown\n||bad.example^$unknown\n";CHECK(subscriptions.RefreshAsync(true,now+7));Wait(subscriptions);CHECK(!subscriptions.Snapshot()[0].error.empty());
    CHECK(subscriptions.SetEnabled("easylist",false,false,error));CHECK(subscriptions.Sources().empty());auto count=fetched.load();CHECK(!subscriptions.RefreshAsync(true));CHECK(fetched==count);
  }
  {
    // A disable action during an in-flight authorized fetch prevents publication.
    std::atomic<bool> entered{false},release{false};auto slow=[&](const std::string&,std::string& body,std::string&){entered=true;for(int i=0;i<2000&&!release;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));body=List("race.example");return true;};
    FilterSubscriptions subscriptions(profile,suffix,slow);CHECK(subscriptions.SetEnabled("easylist",true,true,error));CHECK(subscriptions.RefreshAsync(true,now+8));for(int i=0;i<2000&&!entered;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));CHECK(entered);CHECK(subscriptions.SetEnabled("easylist",false,false,error));release=true;Wait(subscriptions);CHECK(subscriptions.Sources().empty());CHECK(subscriptions.Snapshot()[0].sha256.size()==64);CHECK(subscriptions.SetEnabled("easylist",true,true,error));CHECK(subscriptions.Sources()[0].text!=List("race.example"));CHECK(subscriptions.SetEnabled("easylist",false,false,error));
  }
  {
    FilterSubscriptions subscriptions(profile,suffix,fetch);CHECK(!subscriptions.Snapshot()[0].enabled);CHECK(subscriptions.Snapshot()[0].cached);CHECK(subscriptions.SetEnabled("easylist",true,true,error));
  }
  {
    auto subscriptions=std::make_shared<FilterSubscriptions>(profile,suffix,fetch);CHECK(subscriptions->Snapshot()[0].enabled);unsigned notifications=0;
    auto owner=CreateWindowExW(0,L"STATIC",L"Owned subscription QA",WS_OVERLAPPED,0,0,100,100,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);CHECK(owner);
    auto panel=OpenFilterSubscriptionWindow(owner,subscriptions,false,[&]{++notifications;});CHECK(panel);ShowWindow(panel,SW_HIDE);
    CHECK(SendMessageW(GetDlgItem(panel,4111),CB_GETCOUNT,0,0)==2);CHECK(SendMessageW(GetDlgItem(panel,4112),BM_GETCHECK,0,0)==BST_CHECKED);CHECK(GetWindowTextLengthW(GetDlgItem(panel,4118))>200);
    SendMessageW(GetDlgItem(panel,4112),BM_CLICK,0,0);CHECK(!subscriptions->Snapshot()[0].enabled);CHECK(notifications==1);CHECK(!IsWindowEnabled(GetDlgItem(panel,4113)));
    SendMessageW(GetDlgItem(panel,4111),CB_SETCURSEL,1,0);SendMessageW(panel,WM_COMMAND,MAKEWPARAM(4111,CBN_SELCHANGE),reinterpret_cast<LPARAM>(GetDlgItem(panel,4111)));CHECK(SendMessageW(GetDlgItem(panel,4112),BM_GETCHECK,0,0)==BST_UNCHECKED);
    SendMessageW(GetDlgItem(panel,4116),BM_CLICK,0,0);CHECK(!IsWindow(panel));CHECK(DestroyWindow(owner));CHECK(subscriptions->SetEnabled("easylist",true,true,error));
  }
  // Cache corruption never installs arbitrary policy; recover only validated
  // prior data, with an explicit status and rejected bytes left untouched.
  CHECK(WriteStateFile(profile/L"shield-subscription-0.cache","corrupt"));
  {
    FilterSubscriptions subscriptions(profile,suffix,fetch);CHECK(subscriptions.Snapshot()[0].enabled);CHECK(subscriptions.Snapshot()[0].cached);CHECK(subscriptions.Snapshot()[0].recovered);CHECK(!subscriptions.Snapshot()[0].error.empty());CHECK(subscriptions.Sources()[0].text==List("ads.example"));
  }
  CHECK(WriteStateFile(profile/L"shield-subscriptions.state","bogus-consent"));
  {
    FilterSubscriptions subscriptions(profile,suffix,fetch);CHECK(!subscriptions.Snapshot()[0].enabled);CHECK(!subscriptions.RefreshAsync(true));CHECK(!subscriptions.Snapshot()[0].error.empty());
  }
  {
    auto cache=profile/L"shield-subscription-0.cache",outside=profile/L"owned-link-target.txt";CHECK(WriteStateFile(outside,"owned marker"));CHECK(std::filesystem::remove(cache));
    if(CreateSymbolicLinkW(cache.c_str(),outside.c_str(),SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)){
      FilterSubscriptions subscriptions(profile,suffix,fetch);CHECK(subscriptions.Snapshot()[0].recovered);CHECK(subscriptions.SetEnabled("easylist",true,true,error));next=List("link.example");CHECK(subscriptions.RefreshAsync(true,now+9));Wait(subscriptions);CHECK(!subscriptions.Snapshot()[0].error.empty());CHECK(subscriptions.Sources()[0].text!=next);
      std::string marker;CHECK(ReadStateFile(outside,marker,64)==StateRead::Read&&marker=="owned marker");CHECK(DeleteFileW(cache.c_str()));std::cout<<"PASS subscription cache reparse leaf refusal\n";
    }else{std::cout<<"NOT TESTED cache reparse leaf fixture: Windows symlink privilege unavailable ("<<GetLastError()<<")\n";}
  }
  FilterSubscriptions relative(L"relative-profile",suffix,fetch);CHECK(!relative.SetEnabled("easylist",true,true,error));
  std::string body;CHECK(!FilterSubscriptions::Download("http://easylist.to/easylist/easylist.txt",body,error));CHECK(!FilterSubscriptions::Download("https://evil.example/easylist.txt",body,error));
  // Owned unique temporary fixture only; no browser/user profile cleanup.
  for(const auto& entry:std::filesystem::directory_iterator(profile))CHECK(std::filesystem::remove(entry.path()));CHECK(std::filesystem::remove(profile));
  std::cout<<"PASS subscription consent/cache/update/rollback/backoff/parser/path/publication/custom retention/disable race boundaries\n";return 0;
}
