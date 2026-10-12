#include "core/session.h"
#include "core/state_file.h"
#include <windows.h>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace turan {
namespace {
bool Read(const std::filesystem::path& path,SessionState& result) {
  std::string bytes;if(ReadStateFile(path,bytes,2*1024*1024)!=StateRead::Read)return false;
  std::istringstream in(bytes);SessionState value;std::string header,kind;
  unsigned version=0;
  if(!(in>>header>>version>>value.clean>>value.active)||header!="TURAN_SESSION"||version!=1)return false;
  while(in>>kind) {
    SessionTab tab;
    if(kind!="T"&&kind!="C")return false;
    if(!(in>>std::quoted(tab.title)>>std::quoted(tab.url)>>tab.pinned>>tab.muted>>std::quoted(tab.group)>>tab.color))return false;
    if(tab.title.size()>8192||tab.url.size()>65536||tab.group.size()>128||tab.color>7||!AllowedNavigation(tab.url)||tab.url=="about:blank")return false;
    auto& list=kind=="T"?value.tabs:value.closed;
    list.push_back(std::move(tab));if(list.size()>(kind=="T"?256:20))return false;
  }
  if(!in.eof())return false;
  if(!value.tabs.empty()&&value.active>=value.tabs.size())value.active=0;
  result=std::move(value);return true;
}
}
bool SessionJournal::Load(SessionState& state) const {
  return Read(root_/L"session-v1.txt",state)||Read(root_/L"session-v1.bak",state);
}
bool SessionJournal::Save(const SessionState& state) const {
  if(state.tabs.size()>256||state.closed.size()>20)return false;
  std::error_code ec;std::filesystem::create_directories(root_,ec);if(ec)return false;
  auto path=root_/L"session-v1.txt",backup=root_/L"session-v1.bak";
  std::ostringstream out;
  out<<"TURAN_SESSION 1 "<<state.clean<<' '<<state.active<<'\n';
  auto write=[&](const char* kind,const std::vector<SessionTab>& list) {
    for(auto& t:list) {
      if(!AllowedNavigation(t.url)||t.url=="about:blank"||t.title.size()>8192||t.url.size()>65536||t.group.size()>128||t.color>7)return false;
      out<<kind<<' '<<std::quoted(t.title)<<' '<<std::quoted(t.url)<<' '<<t.pinned<<' '<<t.muted<<' '<<std::quoted(t.group)<<' '<<t.color<<'\n';
    }return true;
  };
  if(!write("T",state.tabs)||!write("C",state.closed))return false;
  auto bytes=out.str();if(bytes.size()>2*1024*1024)return false;
  // Only back up a validated journal; a corrupted primary cannot replace recovery.
  SessionState previous;
  if(Read(path,previous)) {
    std::string prior;if(ReadStateFile(path,prior,2*1024*1024)!=StateRead::Read||!WriteStateFile(backup,prior))return false;
  }
  return WriteStateFile(path,bytes);
}
}
