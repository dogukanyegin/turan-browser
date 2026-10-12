#include "privacy/filter_engine.h"
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#define CHECK(x) do {if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<'\n';return 1;}}while(false)
int main() {
  using namespace turan;
  auto suffix=std::make_shared<PublicSuffix>();std::string error;
  std::ifstream input(std::filesystem::path(TURAN_SOURCE_DIR)/L"resources/public_suffix_list.dat",std::ios::binary);std::ostringstream psl;psl<<input.rdbuf();
  CHECK(suffix->Load(psl.str(),error));CHECK(suffix->size()>9000);
  CHECK(suffix->Site("a.example.co.uk")=="example.co.uk");CHECK(suffix->Site("a.example.com")=="example.com");
  CHECK(suffix->Site("a.b.ck")=="a.b.ck");CHECK(suffix->Site("a.www.ck")=="www.ck");
  CHECK(suffix->ThirdParty("https://a.blogspot.com","https://b.blogspot.com"));
  CHECK(!suffix->ThirdParty("https://a.example.co.uk","https://b.example.co.uk"));
  CHECK(suffix->ThirdParty("https://evil-example.co.uk","https://example.co.uk"));
  CHECK(PublicSuffix::Domain("BÜCHER.de")=="xn--bcher-kva.de");
  CHECK(suffix->Site("127.0.0.1")=="127.0.0.1");CHECK(suffix->Site("[::1]")=="::1");
  auto previous=suffix->Site("a.example.co.uk");CHECK(!suffix->Load("bad..domain\n",error));CHECK(suffix->Site("a.example.co.uk")==previous);
  FilterReport report;
  auto compile=[&](std::string rules){return FilterEngine::Compile({{"test",std::move(rules),FilterCategory::Advertisement}},suffix,report);};
  auto engine=compile("||ads.example^$third-party,script\n@@||ads.example/allowed.js$script\n/ads/*$image\n|https://exact.example/a|$xmlhttprequest\n");
  CHECK(engine&&report.network==4&&report.invalid==0&&report.unsupported==0);
  auto check=[&](std::string url,FilterType type=FilterType::Script,std::string document="https://site.example/a"){return engine->Check({std::move(url),std::move(document),type});};
  CHECK(check("https://cdn.ads.example/x.js").blocked);CHECK(check("https://ads.example:8443/x.js").blocked);
  CHECK(!check("https://badads.example/x.js").blocked);CHECK(!check("https://ads.example.evil/x.js").blocked);
  CHECK(!check("https://good.example/?https://ads.example/x.js").blocked);
  CHECK(!check("https://ads.example/x.js",FilterType::Script,"https://sub.ads.example").blocked);
  CHECK(!check("https://ads.example/x.js",FilterType::Image).blocked);
  CHECK(check("https://ads.example/allowed.js").exception);CHECK(!check("https://ads.example/allowed.js").blocked);
  CHECK(check("https://ads.example:8443/allowed.js").blocked); // A literal slash does not skip a URL port.
  CHECK(check("https://cdn.example/ads/1.png",FilterType::Image).blocked);
  CHECK(check("https://exact.example/a",FilterType::Xhr).blocked);CHECK(!check("https://exact.example/ab",FilterType::Xhr).blocked);
  CHECK(!check("https://exact.example/a",FilterType::Document).blocked);
  engine=compile("||ads.example^$xhr\n@@||ads.example^*/allowed$xhr\n");CHECK(check("http://ads.example:8443/allowed",FilterType::Xhr).exception);
  engine=compile("/AdBox/$match-case,~image,domain=site.example|~private.site.example\n");CHECK(engine&&report.network==1);
  CHECK(check("https://cdn.example/AdBox/x").blocked);CHECK(!check("https://cdn.example/adbox/x").blocked);
  CHECK(!check("https://cdn.example/AdBox/x",FilterType::Image).blocked);
  CHECK(!check("https://cdn.example/AdBox/x",FilterType::Script,"https://private.site.example").blocked);
  CHECK(!check("https://cdn.example/AdBox/x",FilterType::Script,"https://othersite.example").blocked);
  engine=compile("/track[0-9]+\\.js$/$script\n||ws.example^$websocket\n");CHECK(engine&&report.network==2);
  CHECK(check("https://cdn.example/track91.js").blocked);CHECK(!check("https://cdn.example/track91.jsx").blocked);
  CHECK(check("wss://ws.example/socket",FilterType::Websocket).blocked);
  engine=compile("||ads.example^\n@@||site.example^$document\n");CHECK(!check("https://ads.example/x").blocked);
  engine=compile("||ads.example^\n||ads.example^$badfilter\n");CHECK(report.disabled==1&&report.network==0);CHECK(!check("https://ads.example/x").blocked);
  engine=compile("||ads.example^$script,domain=site.example\n||ads.example^$domain=site.example,script,badfilter\n");CHECK(report.disabled==1);
  engine=compile("||ads.example^$important\n@@||ads.example^\n");CHECK(check("https://ads.example/x").blocked);
  engine=compile("||ads.example^$redirect=unsafe\n||ads.example^$unknown\n||ads.example^$script,~script\n/([a-z]+)\\1/$script\n");
  CHECK(engine&&report.unsupported==3&&report.invalid==1&&report.network==0);CHECK(!check("https://ads.example/x").blocked);
  engine=compile("##.advert\nsite.example##.specific\nsite.example#@#.advert\n~site.example##.outside\nsite.example##:has(.promoted)\nexample.com#$#body{background:url(https://exfil.invalid)}\n##+js(unsafe)\n");
  CHECK(engine&&report.cosmetic==5&&report.unsupported==2);
  auto cosmetic=engine->Cosmetics("https://site.example");CHECK(cosmetic.size()==2);CHECK(std::find(cosmetic.begin(),cosmetic.end(),".advert")==cosmetic.end());
  engine=compile("##.generic\nsite.example##.specific\n@@||site.example^$generichide\n");CHECK(engine->Cosmetics("https://site.example")==std::vector<std::string>{".specific"});
  engine=compile("##.generic\nsite.example##.specific\n@@||site.example^$elemhide\n");CHECK(engine->Cosmetics("https://site.example").empty());
  engine=compile("||ads.example^\n||specific.example^$domain=site.example\n@@||site.example^$genericblock\n");CHECK(!check("https://ads.example/x").blocked);CHECK(check("https://specific.example/x").blocked);
  auto valid=engine;auto huge=FilterEngine::Compile({{"oversize",std::string(24*1024*1024+1,'a')}},suffix,report);CHECK(!huge&&!report.ok);CHECK(valid->Check({"https://specific.example/x","https://site.example",FilterType::Script}).blocked);
  CHECK(!FilterEngine::Compile({{"invalid UTF-8",std::string("##.\xFF")}},suffix,report));
  CHECK(valid->Check({"https://specific.example/"+std::string(65536,'a'),"https://site.example",FilterType::Script}).blocked);
  auto start=std::chrono::steady_clock::now();
  engine=compile("/(a+)+b/$script\n");CHECK(engine);CHECK(!check("https://cdn.example/"+std::string(60000,'a')).blocked);
  CHECK(std::chrono::steady_clock::now()-start<std::chrono::seconds(2));
  std::cout<<"PASS PSL boundaries/IDN/private suffix, ABP anchors/types/domain/party/exception/badfilter, bounded regex and cosmetic selection\n";
}
