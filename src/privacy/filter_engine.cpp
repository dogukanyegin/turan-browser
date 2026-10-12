#include "privacy/filter_engine.h"
#include "core/model.h"
#include <re2/re2.h>
#include <algorithm>
#include <array>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <windows.h>

namespace turan {
namespace {
constexpr uint32_t All=4095;
std::string Lower(std::string text) {for(char& c:text)if(c>='A'&&c<='Z')c+=32;return text;}
bool Related(const std::string& host,const std::string& domain) {
  return host==domain||(host.size()>domain.size()&&host.compare(host.size()-domain.size(),domain.size(),domain)==0&&host[host.size()-domain.size()-1]=='.');
}
std::vector<std::string> Split(const std::string& text,char separator) {
  std::vector<std::string> result;size_t begin=0;
  for(size_t i=0;i<=text.size();++i)if(i==text.size()||text[i]==separator){result.push_back(text.substr(begin,i-begin));begin=i+1;}
  return result;
}
struct Scope {
  std::vector<std::string> include,exclude;
  bool Match(const std::string& host) const {
    for(const auto& d:exclude)if(Related(host,d))return false;
    if(include.empty())return true;
    for(const auto& d:include)if(Related(host,d))return true;return false;
  }
};
bool Domains(const std::string& value,char delimiter,Scope& scope) {
  auto list=Split(value,delimiter);if(list.size()>256)return false;
  for(auto name:list) {
    bool excluded=!name.empty()&&name[0]=='~';if(excluded)name.erase(0,1);
    auto d=PublicSuffix::Domain(name);if(d.empty())return false;
    (excluded?scope.exclude:scope.include).push_back(d);
  }
  for(auto domains:{&scope.include,&scope.exclude}){std::sort(domains->begin(),domains->end());domains->erase(std::unique(domains->begin(),domains->end()),domains->end());}
  return true;
}
uint32_t Type(const std::string& name) {
  static const std::unordered_map<std::string,uint32_t> types={{"document",1},{"subdocument",2},{"script",4},{"image",8},{"stylesheet",16},{"font",32},{"media",64},{"xmlhttprequest",128},{"xhr",128},{"ping",256},{"websocket",512},{"object",1024},{"other",2048}};
  auto found=types.find(name);return found==types.end()?0:found->second;
}
enum class PageException {None,Document,Elemhide,Generichide,Genericblock};
struct Rule {
  std::string raw,source,key,token;
  FilterCategory category;
  Scope scope;
  uint32_t types=All;
  int party=-1;
  bool exception=false,important=false,badfilter=false;
  PageException page=PageException::None;
  std::unique_ptr<RE2> pattern;
};
struct Cosmetic {std::string selector;Scope scope;bool exception=false;};
std::string Pattern(const std::string& text,bool& raw) {
  raw=text.size()>1&&text.front()=='/'&&text.back()=='/';
  if(raw)return text.substr(1,text.size()-2);
  std::string input=text,regex;
  bool domain=input.rfind("||",0)==0;
  if(domain){regex="^(?:https?|wss?)://(?:[^/?#]*@)?(?:[^/?#.:]+\\.)*";input.erase(0,2);}
  else if(!input.empty()&&input.front()=='|'){regex="^";input.erase(0,1);}
  bool end=!input.empty()&&input.back()=='|';if(end)input.pop_back();
  bool in_host=domain;
  for(char c:input) {
    if(c=='^'){regex+="(?:[^A-Za-z0-9_%.\\-]|$)";in_host=false;}
    else if(c=='*')regex+=in_host?"[^/?#]*":".*";
    else if(c=='|')return {};
    else {if(std::string(".\\+?[]{}()$^").find(c)!=std::string::npos)regex+='\\';regex+=c;if(c=='/')in_host=false;}
  }
  if(end)regex+='$';return regex;
}
std::string Token(const std::string& pattern) {
  std::string longest,run;
  for(size_t i=0;i<=pattern.size();++i) {
    char c=i<pattern.size()?pattern[i]:'*';
    if(c=='*'||c=='^'||c=='|'||static_cast<unsigned char>(c)>=128) {if(run.size()>longest.size())longest=run;run.clear();}
    else run+=c;
  }
  return longest.size()>=8?Lower(longest.substr(0,8)):longest.size()>=3?Lower(longest.substr(0,3)):std::string();
}
std::string Key(const Rule& r,const std::string& pattern,bool sensitive) {
  std::ostringstream key;key<<pattern<<'\n'<<sensitive<<r.exception<<r.important<<':'<<r.types<<':'<<r.party<<':'<<static_cast<int>(r.page);
  for(const auto& d:r.scope.include)key<<'+'<<d;for(const auto& d:r.scope.exclude)key<<'-'<<d;return key.str();
}
// Unsupported options reject the entire rule, preserving its constraints.
bool ParseNetwork(std::string text,const FilterSource& source,Rule& r,std::string& error,bool& unsupported) {
  r.raw=text;r.source=source.id;r.category=source.category;
  if(text.rfind("@@",0)==0){r.exception=true;text.erase(0,2);}
  std::string options;
  // A regex may contain $, including its end anchor. Options start after the
  // closing regex slash, never at an interior regex dollar character.
  size_t dollar=std::string::npos;
  if(text.size()>1&&text.front()=='/') {
    auto close=text.rfind('/');
    if(close>0&&close!=std::string::npos&&close+1<text.size()&&text[close+1]=='$')dollar=close+1;
    else if(close==0||close+1!=text.size())dollar=text.find('$');
  } else dollar=text.find('$');
  if(dollar!=std::string::npos){options=text.substr(dollar+1);text.resize(dollar);if(options.empty()){error="Empty options";return false;}}
  if(text.empty()){error="Empty pattern";return false;}
  uint32_t positive=0,negative=0;bool sensitive=false,party_seen=false;std::unordered_set<std::string> seen;
  if(!options.empty())for(auto option:Split(options,',')) {
    auto lower=Lower(option);bool invert=!lower.empty()&&lower.front()=='~';auto name=invert?lower.substr(1):lower;
    if(name.empty()||!seen.insert(lower).second){error="Empty/duplicate option";return false;}
    if(name=="third-party"||name=="3p"||name=="1p") {
      int party=(name=="1p")?0:1;if(invert)party=1-party;
      if(party_seen&&r.party!=party){error="Contradictory party options";return false;}r.party=party;party_seen=true;
    } else if(name=="match-case"&&!invert)sensitive=true;
    else if(name=="important"&&!invert)r.important=true;
    else if(name=="badfilter"&&!invert)r.badfilter=true;
    else if(lower.rfind("domain=",0)==0) {
      if(!r.scope.include.empty()||!r.scope.exclude.empty()||!Domains(option.substr(7),'|',r.scope)){error="Invalid domain scope";return false;}
    } else if((name=="document"||name=="elemhide"||name=="generichide"||name=="genericblock")&&!invert&&r.exception) {
      if(r.page!=PageException::None){error="Multiple page exception modes are not supported";unsupported=true;return false;}
      r.page=name=="document"?PageException::Document:name=="elemhide"?PageException::Elemhide:name=="generichide"?PageException::Generichide:PageException::Genericblock;
    } else if(auto bit=Type(name);bit&&name!="document") {if(invert)negative|=bit;else positive|=bit;}
    else {error="Unsupported option: "+option;unsupported=true;return false;}
  }
  r.types=(positive?positive:All)&~negative;
  if(!r.types){error="No resource types remain";return false;}
  bool raw=false;auto regex=Pattern(text,raw);if(regex.empty()){error="Invalid pattern anchor";return false;}
  RE2::Options config;config.set_case_sensitive(sensitive);config.set_log_errors(false);config.set_max_mem(128*1024);
  // CEF serializes network URLs with ASCII punycode/percent escapes. ASCII
  // wildcard rules can use byte matching without expanding every negated URL
  // character class into a Unicode automaton. Explicit regexes and Unicode
  // literals retain UTF-8 matching. Keep the total compiled-program budget.
  if(!raw&&std::all_of(text.begin(),text.end(),[](unsigned char c){return c<128;}))config.set_encoding(RE2::Options::EncodingLatin1);
  r.pattern=std::make_unique<RE2>(regex,config);
  if(!r.pattern->ok()){error="Unsupported or invalid bounded regex: "+r.pattern->error();unsupported=true;return false;}
  r.key=Key(r,text,sensitive);if(!raw)r.token=Token(text);return true;
}
bool Applies(const Rule& r,const FilterRequest& request,const PublicSuffix& suffix) {
  if(!(r.types&static_cast<uint32_t>(request.type))||!r.scope.Match(PublicSuffix::Domain(Host(request.document))))return false;
  if(r.party>=0&&suffix.ThirdParty(request.url,request.document)!=(r.party==1))return false;
  return RE2::PartialMatch(request.url,*r.pattern);
}
}
struct FilterEngine::Impl {
  std::shared_ptr<const PublicSuffix> suffix;
  std::vector<Rule> rules;
  std::vector<Cosmetic> cosmetics;
  std::unordered_map<std::string,std::vector<size_t>> tokens;
  std::vector<size_t> generic,pages;
  std::vector<size_t> Candidates(const std::string& url) const {
    std::vector<size_t> result=generic;std::unordered_set<std::string> words;
    auto lower=Lower(url);
    for(size_t length:{size_t(3),size_t(8)})for(size_t i=0;i+length<=lower.size();++i) {
      auto word=lower.substr(i,length);if(!words.insert(word).second)continue;
      auto found=tokens.find(word);if(found!=tokens.end())result.insert(result.end(),found->second.begin(),found->second.end());
    }
    std::sort(result.begin(),result.end());result.erase(std::unique(result.begin(),result.end()),result.end());return result;
  }
  bool PageAllowed(PageException kind,const std::string& document) const {
    FilterRequest page{document,document,FilterType::Document};
    for(auto i:pages)if(rules[i].page==kind&&Applies(rules[i],page,*suffix))return true;return false;
  }
};
FilterEngine::FilterEngine():impl_(std::make_unique<Impl>()){}
FilterEngine::~FilterEngine()=default;
std::shared_ptr<const FilterEngine> FilterEngine::Compile(const std::vector<FilterSource>& sources,std::shared_ptr<const PublicSuffix> suffix,FilterReport& report) {
  report={};if(!suffix||suffix->size()==0){report.error="A validated Public Suffix List is required";return {};}
  auto engine=std::shared_ptr<FilterEngine>(new FilterEngine);engine->impl_->suffix=std::move(suffix);
  std::unordered_set<std::string> bad;size_t total=0,programs=0;
  auto issue=[&](const FilterSource& source,size_t line,const std::string& reason,bool unsupported){++(unsupported?report.unsupported:report.invalid);if(report.diagnostics.size()<100)report.diagnostics.push_back({source.id,reason,line});};
  if(sources.size()>32){report.error="Too many filter sources";return {};}
  for(const auto& source:sources) {
    total+=source.text.size();
    if(source.id.empty()||source.id.size()>256||total>24*1024*1024||source.text.find('\0')!=std::string::npos||(!source.text.empty()&&!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,source.text.data(),static_cast<int>(source.text.size()),nullptr,0))){report.error="Filter source size/name/UTF-8/content limit exceeded";return {};}
    std::istringstream in(source.text);std::string text;size_t line=0;
    while(std::getline(in,text)) {
      ++line;if(!text.empty()&&text.back()=='\r')text.pop_back();
      if(line==1&&text.rfind("\xEF\xBB\xBF",0)==0)text.erase(0,3);
      if(text.empty()||text.front()=='!'||text.rfind("[Adblock",0)==0)continue;
      if(text.size()>4096||std::any_of(text.begin(),text.end(),[](unsigned char c){return c<32||c==127;})){issue(source,line,"Invalid rule size/control character",false);continue;}
      if(engine->impl_->rules.size()+engine->impl_->cosmetics.size()>=150000){report.error="Filter rule limit exceeded";return {};}
      auto except=text.find("#@#"),hide=text.find("##");
      auto advanced=text.find("#?#");if(advanced!=std::string::npos||text.find("#$#")!=std::string::npos||text.find("#@$#")!=std::string::npos||text.find("#@?#")!=std::string::npos){issue(source,line,"Unsupported procedural/style/scriptlet syntax",true);continue;}
      if(except!=std::string::npos||hide!=std::string::npos) {
        size_t marker=except!=std::string::npos?except:hide;Cosmetic cosmetic;cosmetic.exception=except!=std::string::npos;cosmetic.selector=text.substr(marker+(cosmetic.exception?3:2));
        if(cosmetic.selector.empty()||cosmetic.selector.size()>2048||(!text.substr(0,marker).empty()&&!Domains(text.substr(0,marker),',',cosmetic.scope))){issue(source,line,"Invalid cosmetic domain/selector",false);continue;}
        if(cosmetic.selector.find("+js(")!=std::string::npos||cosmetic.selector.find(":-abp-")!=std::string::npos||cosmetic.selector.find(":style(")!=std::string::npos||cosmetic.selector.find(":remove(")!=std::string::npos){issue(source,line,"Unsupported executable/procedural cosmetic rule",true);continue;}
        engine->impl_->cosmetics.push_back(std::move(cosmetic));++report.cosmetic;continue;
      }
      Rule rule;std::string error;bool unsupported=false;
      if(!ParseNetwork(text,source,rule,error,unsupported)){issue(source,line,error,unsupported);continue;}
      if(rule.badfilter){bad.insert(rule.key);continue;}
      programs+=static_cast<size_t>(rule.pattern->ProgramSize());
      if(programs>8*1024*1024){report.error="Compiled filter program budget exceeded";return {};}
      engine->impl_->rules.push_back(std::move(rule));
    }
  }
  auto& rules=engine->impl_->rules;
  rules.erase(std::remove_if(rules.begin(),rules.end(),[&](const Rule& r){if(bad.count(r.key)){++report.disabled;return true;}return false;}),rules.end());
  report.network=rules.size();
  for(size_t i=0;i<rules.size();++i) {
    if(rules[i].page!=PageException::None)engine->impl_->pages.push_back(i);
    else if(rules[i].token.empty())engine->impl_->generic.push_back(i);
    else engine->impl_->tokens[rules[i].token].push_back(i);
  }
  report.ok=true;return engine;
}
FilterDecision FilterEngine::Check(const FilterRequest& request) const {
  if(request.url.rfind("http://",0)!=0&&request.url.rfind("https://",0)!=0&&request.url.rfind("ws://",0)!=0&&request.url.rfind("wss://",0)!=0)return {};
  if(request.type==FilterType::Document)return {};
  // Do not turn an oversized tracking URL into an unfiltered network request.
  // The reason/category remains explicit and site exceptions can override it.
  if(request.url.size()>65536||request.document.size()>65536)return {true,false,"[URL budget]","Turan limits","Request exceeds 64 KiB filter URL budget",FilterCategory::Custom};
  if(impl_->PageAllowed(PageException::Document,request.document))return {false,true,"","","Document exception"};
  bool generic_block=impl_->PageAllowed(PageException::Genericblock,request.document);
  const Rule *blocked=nullptr,*allowed=nullptr;
  for(auto index:impl_->Candidates(request.url)) {
    const auto& rule=impl_->rules[index];
    if(generic_block&&!rule.exception&&rule.scope.include.empty())continue;
    if(!Applies(rule,request,*impl_->suffix))continue;
    auto& match=rule.exception?allowed:blocked;if(!match||rule.important)match=&rule;
  }
  const Rule* match=allowed&&(!blocked||!blocked->important||allowed->important)?allowed:blocked;
  if(!match)return {};
  return {!match->exception,match->exception,match->raw,match->source,match->exception?"Network exception":"Network filter match",match->category};
}
std::vector<std::string> FilterEngine::Cosmetics(const std::string& document) const {
  if(document.size()>65536||impl_->PageAllowed(PageException::Document,document)||impl_->PageAllowed(PageException::Elemhide,document))return {};
  const bool generic_hide=impl_->PageAllowed(PageException::Generichide,document);auto host=PublicSuffix::Domain(Host(document));
  std::unordered_set<std::string> allowed,selectors;
  for(const auto& rule:impl_->cosmetics)if(rule.scope.Match(host)) {
    if(rule.exception)allowed.insert(rule.selector);else if(!generic_hide||!rule.scope.include.empty())selectors.insert(rule.selector);
  }
  std::vector<std::string> result;for(const auto& selector:selectors)if(!allowed.count(selector))result.push_back(selector);
  std::sort(result.begin(),result.end());return result;
}
}
