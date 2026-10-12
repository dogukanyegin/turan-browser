#include "privacy/filter_engine.h"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <chrono>
#include <vector>
#include <algorithm>
#include <windows.h>
#include <psapi.h>
namespace {
std::string Read(const std::wstring& path) {std::ifstream in(std::filesystem::path(path),std::ios::binary);std::ostringstream text;text<<in.rdbuf();return text.str();}
}
int wmain(int argc,wchar_t** argv) {
  using namespace turan;
  if(argc!=4)return 2;
  auto suffix=std::make_shared<PublicSuffix>();std::string error;
  if(!suffix->Load(Read(argv[1]),error)){std::cerr<<error;return 3;}
  FilterReport report;auto begin=std::chrono::steady_clock::now();
  auto engine=FilterEngine::Compile({{"EasyList QA",Read(argv[2]),FilterCategory::Advertisement},{"EasyPrivacy QA",Read(argv[3]),FilterCategory::Tracker}},suffix,report);
  auto compiled=std::chrono::steady_clock::now();
  std::cout<<"network="<<report.network<<"\ncosmetic="<<report.cosmetic<<"\nunsupported="<<report.unsupported<<"\ninvalid="<<report.invalid<<"\ndisabled="<<report.disabled<<"\ncompileMs="<<std::chrono::duration<double,std::milli>(compiled-begin).count()<<'\n';
  if(!engine){std::cout<<"error="<<report.error<<'\n';return 4;}
  std::vector<double> times;size_t blocked=0;
  for(size_t i=0;i<1000;++i) {
    std::string url=i%2?"https://example.org/static/negative-control-"+std::to_string(i)+".js":"https://googleads.g.doubleclick.net/pagead/ads?client=fixture";
    auto start=std::chrono::steady_clock::now();auto decision=engine->Check({url,"https://example.org/fixture",FilterType::Script});
    times.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count());blocked+=decision.blocked;
  }
  std::sort(times.begin(),times.end());std::cout<<"sampleRequests=1000\nblockedSamples="<<blocked<<"\nmedianMatchUs="<<times[500]<<"\np95MatchUs="<<times[950]<<'\n';
  PROCESS_MEMORY_COUNTERS_EX memory={};memory.cb=sizeof(memory);
  if(!GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)))return 5;
  std::cout<<"peakPrivateCommitMiB="<<memory.PeakPagefileUsage/(1024.0*1024)<<"\npeakWorkingSetMiB="<<memory.PeakWorkingSetSize/(1024.0*1024)<<'\n';
  for(const auto& d:report.diagnostics)std::cout<<"diagnostic="<<d.source<<':'<<d.line<<':'<<d.reason<<'\n';
  return 0;
}
