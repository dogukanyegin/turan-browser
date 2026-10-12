#include "core/model.h"
#include "core/state_file.h"
#include <algorithm>
#include <sstream>

namespace turan {
namespace {
bool RulesFile(const std::filesystem::path& path,std::string& value,size_t limit,std::string& error) {
  if(ReadStateFile(path,value,limit)!=StateRead::Read){error="Cannot read validated filter input: "+Utf8(path.filename().wstring());return false;}return true;
}
bool Sites(const std::string& text,std::set<std::string>& sites) {
  std::istringstream in(text);std::string line;
  while(std::getline(in,line)) {
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    if(line.empty())continue;
    if(sites.size()>=1000||PublicSuffix::Domain(line)!=line||!sites.insert(line).second)return false;
  }return true;
}
}
void Shield::LoadRules(const std::filesystem::path& path,const std::filesystem::path& suffix,const std::filesystem::path& profile) {
  std::lock_guard<std::mutex> guard(mutex_);profile_=profile;report_={};subscriptions_.clear();custom_.clear();exceptions_.clear();engine_.reset();++policy_revision_;
  std::string text,error;
  auto psl=std::make_shared<PublicSuffix>();
  if(!RulesFile(suffix,text,2*1024*1024,error)||!psl->Load(text,error)){report_.error=error;return;}
  suffix_=psl;
  if(!RulesFile(path,text,64*1024,error)){report_.error=error;return;}
  // Preserve the existing curated domain list, assigning the category explicitly.
  // Plain domains never become accidental unanchored URL substring rules.
  const std::set<std::string> trackers={"google-analytics.com","googletagmanager.com","scorecardresearch.com","connect.facebook.net"};
  std::string ads,tracking;std::istringstream lines(text);std::string line;
  while(std::getline(lines,line)) {
    if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty()||line.front()=='#')continue;
    auto domain=PublicSuffix::Domain(line);if(domain.empty()){report_.error="Invalid curated filter domain";return;}
    (trackers.count(domain)?tracking:ads)+="||"+domain+"^$third-party\n";
  }
  starter_={{"Turan starter ads",ads,FilterCategory::Advertisement},{"Turan starter trackers",tracking,FilterCategory::Tracker}};
  auto sources=starter_;std::string warning;
  if(!profile_.empty()) {
    auto state=ReadStateFile(profile_/L"shield-custom.txt",custom_,64*1024);
    if(state==StateRead::Error){custom_.clear();warning="Custom rules could not be read; file preserved";}
    if(state==StateRead::Read) {
      FilterReport custom_report;
      auto tested=FilterEngine::Compile({{"Custom",custom_}},psl,custom_report);
      if(!tested||custom_report.invalid||custom_report.unsupported) {
        warning="Invalid custom rules preserved; only starter rules active";custom_.clear();
        std::string backup;
        if(ReadStateFile(profile_/L"shield-custom.txt.bak",backup,64*1024)==StateRead::Read) {
          tested=FilterEngine::Compile({{"Custom",backup}},psl,custom_report);
          if(tested&&!custom_report.invalid&&!custom_report.unsupported){custom_=backup;warning="Custom rules recovered from backup; rejected file preserved";}
        }
      }
    }
    std::string sites;state=ReadStateFile(profile_/L"shield-sites.txt",sites,64*1024);
    if(state==StateRead::Error||(state==StateRead::Read&&!Sites(sites,exceptions_))){exceptions_.clear();warning+=" Invalid site exceptions preserved";}
  }
  sources.push_back({"Custom",custom_});engine_=FilterEngine::Compile(sources,psl,report_);report_.error=warning;
}
bool Shield::ThirdParty(const std::string& url,const std::string& first) const {
  std::lock_guard<std::mutex> guard(mutex_);return suffix_?suffix_->ThirdParty(url,first):IsThirdParty(url,first);
}
bool Shield::SiteEnabled(int browser,const std::string& url) const {
  auto host=PublicSuffix::Domain(Host(url));std::lock_guard<std::mutex> guard(mutex_);
  auto state=browsers_.find(browser);
  if(state==browsers_.end())return true;
  return !(state->second.incognito?state->second.exceptions:exceptions_).count(host);
}
FilterDecision Shield::Check(int browser,const FilterRequest& request,uint64_t generation) {
  if(!enabled)return {};
  std::shared_ptr<const FilterEngine> engine;
  {std::lock_guard<std::mutex> guard(mutex_);engine=engine_;}
  if(!engine)return {false,false,"","","Filter engine unavailable"};
  if(!SiteEnabled(browser,request.document))return {false,true,"","Turan site exception","Shield disabled for this document host"};
  auto decision=engine->Check(request);
  if(decision.blocked) {
    ++blocked;std::lock_guard<std::mutex> guard(mutex_);auto state=browsers_.find(browser);
    if(state!=browsers_.end()&&(!generation||generation==state->second.generation)) {
      auto& snapshot=state->second.snapshot;
      switch(decision.category){case FilterCategory::Advertisement:++snapshot.ads;break;case FilterCategory::Tracker:++snapshot.trackers;break;default:++snapshot.custom;}
      if(snapshot.events.size()==100)snapshot.events.erase(snapshot.events.begin());
      auto record=decision;record.rule.resize(std::min(record.rule.size(),size_t(512)));
      snapshot.events.push_back({request.url.substr(0,1024),request.document.substr(0,512),std::move(record)});
    }
  }return decision;
}
bool Shield::Block(const std::string& url,const std::string& first) {return Check(-1,{url,first,FilterType::Other}).blocked;}
std::vector<std::string> Shield::Cosmetics(int browser,const std::string& document) const {
  if(!enabled||!SiteEnabled(browser,document))return {};
  std::shared_ptr<const FilterEngine> engine;{std::lock_guard<std::mutex> guard(mutex_);engine=engine_;}
  return engine?engine->Cosmetics(document):std::vector<std::string>();
}
void Shield::Register(int browser,bool incognito) {std::lock_guard<std::mutex> guard(mutex_);if(browsers_.size()<256)browsers_.emplace(browser,BrowserState{incognito});}
void Shield::Begin(int browser) {std::lock_guard<std::mutex> guard(mutex_);auto state=browsers_.find(browser);if(state!=browsers_.end()){state->second.snapshot={};++state->second.generation;}}
uint64_t Shield::Generation(int browser) const {std::lock_guard<std::mutex> guard(mutex_);auto state=browsers_.find(browser);return state==browsers_.end()?0:state->second.generation;}
void Shield::Forget(int browser) {std::lock_guard<std::mutex> guard(mutex_);browsers_.erase(browser);}
Shield::Snapshot Shield::Events(int browser) const {std::lock_guard<std::mutex> guard(mutex_);auto state=browsers_.find(browser);return state==browsers_.end()?Snapshot{}:state->second.snapshot;}
FilterReport Shield::Report() const {std::lock_guard<std::mutex> guard(mutex_);return report_;}
std::shared_ptr<const PublicSuffix> Shield::Suffix() const {std::lock_guard<std::mutex> guard(mutex_);return suffix_;}
std::string Shield::CustomRules() const {std::lock_guard<std::mutex> guard(mutex_);return custom_;}
bool Shield::SaveCustom(const std::string& text,std::string& error) {
  if(text.size()>64*1024||std::count(text.begin(),text.end(),'\n')>2000){error="Custom rules exceed 64 KiB / 2000 lines";return false;}
  std::shared_ptr<const PublicSuffix> suffix;std::vector<FilterSource> sources;uint64_t revision;
  {std::lock_guard<std::mutex> guard(mutex_);suffix=suffix_;sources=starter_;sources.insert(sources.end(),subscriptions_.begin(),subscriptions_.end());revision=policy_revision_;}
  // Optional lists may legitimately contain unsupported dialects; custom rules
  // remain strict and must not inherit that tolerance.
  FilterReport custom_report;auto custom_test=FilterEngine::Compile({{"Custom",text}},suffix,custom_report);
  if(!custom_test||custom_report.invalid||custom_report.unsupported) {
    // Compilation may fail without a per-rule diagnostic (for example when
    // the engine or source budget is unavailable). Never index an empty list.
    if(!custom_report.error.empty())error=custom_report.error;
    else if(!custom_report.diagnostics.empty()) {
      const auto& diagnostic=custom_report.diagnostics.front();
      error="Rejected rule: "+diagnostic.reason+" (line "+std::to_string(diagnostic.line)+")";
    } else error="Custom filter rules could not be validated";
    return false;
  }
  custom_test.reset();sources.push_back({"Custom",text});FilterReport report;auto engine=FilterEngine::Compile(sources,suffix,report);
  if(!engine){error=report.error;return false;}
  std::lock_guard<std::mutex> guard(mutex_);
  if(revision!=policy_revision_){error="Filter policy changed during custom-rule compilation; retry";return false;}
  if(profile_.empty()||!WriteStateFile(profile_/L"shield-custom.txt.bak",custom_)||!WriteStateFile(profile_/L"shield-custom.txt",text)){error="Cannot atomically save custom rules; previous policy retained";return false;}
  engine_=std::move(engine);report_=std::move(report);custom_=text;++policy_revision_;return true;
}
bool Shield::SetSubscriptions(const std::vector<FilterSource>& subscriptions,std::string& error) {
  error.clear();if(subscriptions.size()>2){error="Subscription count limit exceeded";return false;}
  std::set<std::string> names;
  for(const auto& source:subscriptions)if((source.id!="EasyList"&&source.id!="EasyPrivacy")||source.text.size()>5*1024*1024||!names.insert(source.id).second){error="Invalid subscription publication input";return false;}
  std::vector<FilterSource> sources;std::shared_ptr<const PublicSuffix> suffix;uint64_t revision;
  {std::lock_guard<std::mutex> guard(mutex_);suffix=suffix_;sources=starter_;sources.push_back({"Custom",custom_});revision=policy_revision_;}
  sources.insert(sources.end(),subscriptions.begin(),subscriptions.end());FilterReport report;
  auto engine=FilterEngine::Compile(sources,suffix,report);if(!engine){error=report.error;return false;}
  std::lock_guard<std::mutex> guard(mutex_);
  if(revision!=policy_revision_){error="Filter policy changed during subscription compilation; retry";return false;}
  subscriptions_=subscriptions;engine_=std::move(engine);report_=std::move(report);++policy_revision_;return true;
}
bool Shield::SetSiteEnabled(int browser,const std::string& url,bool enabled_site,std::string& error) {
  auto host=PublicSuffix::Domain(Host(url));if(host.empty()){error="Only validated web document hosts support site exceptions";return false;}
  std::lock_guard<std::mutex> guard(mutex_);auto state=browsers_.find(browser);if(state==browsers_.end()){error="Browser already closed";return false;}
  auto sites=state->second.incognito?state->second.exceptions:exceptions_;
  if(enabled_site)sites.erase(host);else sites.insert(host);
  if(sites.size()>1000){error="Site exception limit reached";return false;}
  if(state->second.incognito)state->second.exceptions=std::move(sites);
  else {
    std::string text;for(const auto& site:sites)text+=site+'\n';
    if(profile_.empty()||!WriteStateFile(profile_/L"shield-sites.txt",text)){error="Cannot atomically save site exception";return false;}exceptions_=std::move(sites);
  }return true;
}
}
