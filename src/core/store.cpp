#include "core/model.h"
#include "core/state_file.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace turan {
namespace {
constexpr size_t Limit=16*1024*1024;
struct Data {Settings settings;std::vector<Entry> bookmarks,history;std::vector<SavedDownload> downloads;std::vector<PermissionRule> permissions;std::vector<std::string> unknown;};
bool EntryValid(const Entry& entry) {
  return entry.title.size()<8192&&entry.url.size()<65536&&entry.title.find('\0')==std::string::npos&&
      entry.url.find('\0')==std::string::npos&&AllowedNavigation(entry.url)&&entry.url!="about:blank";
}
bool Bit(std::istream& in,bool& value) {
  int bit=0;if(!(in>>bit)||(bit!=0&&bit!=1))return false;value=bit!=0;return true;
}
bool DownloadValid(const SavedDownload& entry) {
  return entry.name.size()<8192&&entry.url.size()<65536&&entry.path.size()<32768&&entry.finished>0&&
      entry.finished<=253402300799ULL&&entry.bytes<=9223372036854775807ULL&&
      entry.name.find('\0')==std::string::npos&&entry.url.find('\0')==std::string::npos&&
      entry.path.find('\0')==std::string::npos&&AllowedNavigation(entry.url)&&entry.url!="about:blank"&&
      (entry.path.empty()?entry.canceled:std::filesystem::path(Wide(entry.path)).is_absolute());
}
bool Parse(const std::string& bytes,Data& result) {
  if(bytes.empty()||bytes.size()>Limit||bytes.find('\0')!=std::string::npos)return false;
  std::istringstream in(bytes);Data data;std::string kind;bool settings=false,restore=false;
  while(in>>kind) {
    if(kind=="S") {
      if(settings||!Bit(in,data.settings.shield)||!Bit(in,data.settings.third_party_cookies)||
          !Bit(in,data.settings.https_only)||!Bit(in,data.settings.doh)||!Bit(in,data.settings.dark)||
          !Bit(in,data.settings.turkish))return false;
      settings=true;
    } else if(kind=="R") {
      if(restore||!Bit(in,data.settings.restore_session))return false;restore=true;
    } else if(kind=="B"||kind=="H") {
      Entry entry;
      if(!(in>>std::quoted(entry.title)>>std::quoted(entry.url))||!EntryValid(entry))return false;
      (kind=="B"?data.bookmarks:data.history).push_back(std::move(entry));
      if(data.bookmarks.size()+data.history.size()>4000)return false;
    } else if(kind=="D") {
      SavedDownload entry;
      if(!(in>>std::quoted(entry.name)>>std::quoted(entry.url)>>std::quoted(entry.path)>>entry.bytes>>entry.finished)||
          !Bit(in,entry.canceled)||!DownloadValid(entry))return false;
      data.downloads.push_back(std::move(entry));if(data.downloads.size()>1000)return false;
    } else if(kind=="Q") {
      PermissionRule rule;
      if(!(in>>std::quoted(rule.origin)>>std::quoted(rule.top)>>rule.kind)||!Bit(in,rule.allow)||!PermissionRuleValid(rule))return false;
      auto duplicate=std::find_if(data.permissions.begin(),data.permissions.end(),[&](const PermissionRule& previous){return previous.origin==rule.origin&&previous.top==rule.top&&previous.kind==rule.kind;});
      if(duplicate!=data.permissions.end())return false;
      data.permissions.push_back(std::move(rule));if(data.permissions.size()>512)return false;
    } else {
      // Retain opaque future records instead of dropping them during an older-version save.
      std::string rest;std::getline(in,rest);auto record=kind+rest;
      if(record.size()>8192||data.unknown.size()>=128)return false;
      data.unknown.push_back(std::move(record));
    }
  }
  if(!in.eof()||!settings)return false;result=std::move(data);return true;
}
bool Read(const std::filesystem::path& path,Data& result,std::string& bytes,StateRead& status) {
  status=ReadStateFile(path,bytes,Limit);return status==StateRead::Read&&Parse(bytes,result);
}
}
Store::Store(std::filesystem::path path):root(std::move(path)) {}
StoreLoad Store::Load() {
  settings=Settings();bookmarks.clear();history.clear();downloads.clear();permissions.clear();preserved_records_.clear();writable_=false;recovered_=false;
  Data data;std::string bytes;StateRead primary,backup;StoreLoad loaded=StoreLoad::Loaded;
  if(!Read(root/L"browser-state.txt",data,bytes,primary)) {
    if(Read(root/L"browser-state.bak",data,bytes,backup))loaded=StoreLoad::Recovered;
    else if(primary==StateRead::Missing&&backup==StateRead::Missing){writable_=true;return StoreLoad::Fresh;}
    else return StoreLoad::Invalid;
  }
  settings=data.settings;bookmarks=std::move(data.bookmarks);history=std::move(data.history);
  downloads=std::move(data.downloads);
  permissions=std::move(data.permissions);
  preserved_records_=std::move(data.unknown);writable_=true;recovered_=loaded==StoreLoad::Recovered;return loaded;
}
bool Store::Save() const {
  if(!writable_||bookmarks.size()+history.size()>4000||downloads.size()>1000)return false;
  std::ostringstream out;
  out<<"S "<<settings.shield<<' '<<settings.third_party_cookies<<' '<<settings.https_only<<' '<<settings.doh<<' '<<settings.dark<<' '<<settings.turkish<<'\n';
  out<<"R "<<settings.restore_session<<'\n';
  for(auto& list:{&bookmarks,&history})for(auto& entry:*list) {
    if(!EntryValid(entry))return false;
    out<<(list==&bookmarks?"B ":"H ")<<std::quoted(entry.title)<<' '<<std::quoted(entry.url)<<'\n';
  }
  for(auto& record:preserved_records_)out<<record<<'\n';
  for(auto& entry:downloads) {
    if(!DownloadValid(entry))return false;
    out<<"D "<<std::quoted(entry.name)<<' '<<std::quoted(entry.url)<<' '<<std::quoted(entry.path)<<' '<<entry.bytes<<' '<<entry.finished<<' '<<entry.canceled<<'\n';
  }
  for(auto& rule:permissions) {
    if(!PermissionRuleValid(rule))return false;
    out<<"Q "<<std::quoted(rule.origin)<<' '<<std::quoted(rule.top)<<' '<<rule.kind<<' '<<rule.allow<<'\n';
  }
  auto bytes=out.str();Data check;if(!Parse(bytes,check))return false;
  std::error_code ec;std::filesystem::create_directories(root,ec);if(ec)return false;
  auto path=root/L"browser-state.txt";Data previous;std::string prior;StateRead status;
  if(Read(path,previous,prior,status)) {
    if(!WriteStateFile(root/L"browser-state.bak",prior))return false;
  } else if(status!=StateRead::Missing) {
    // Recovery never replaces the only known-good backup with damaged bytes.
    if(!QuarantineStateFile(path))return false;
  }
  return WriteStateFile(path,bytes);
}
void Store::Visit(const Entry& entry,bool private_tab) {
  if(private_tab||!EntryValid(entry))return;
  if(!history.empty()&&history.back().url==entry.url)history.back()=entry;
  else history.push_back(entry);
  if(history.size()>2000)history.erase(history.begin());
}
void Store::Bookmark(const Entry& entry) {
  if(!EntryValid(entry))return;
  auto it=std::find_if(bookmarks.begin(),bookmarks.end(),[&](const Entry& b){return b.url==entry.url;});
  if(it!=bookmarks.end())bookmarks.erase(it);
  else if(bookmarks.size()+history.size()<4000)bookmarks.push_back(entry);
}
void Store::RememberDownload(const SavedDownload& entry,bool incognito) {
  if(incognito||!DownloadValid(entry))return;
  downloads.push_back(entry);if(downloads.size()>1000)downloads.erase(downloads.begin());
}
}
