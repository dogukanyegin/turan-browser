#include "core/session.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#define CHECK(x) do {if(!(x)){std::cerr<<"FAILED "<<__LINE__<<": "<<#x<<'\n';return 1;}}while(false)
int main() {
  using namespace turan;
  wchar_t temp[MAX_PATH];GetTempPathW(MAX_PATH,temp);
  const auto root=std::filesystem::path(temp)/(L"turan-session-test-"+std::to_wstring(GetCurrentProcessId()));
  SessionJournal journal(root);SessionState state,loaded;
  state.clean=false;state.active=1;
  state.tabs.push_back({"Türkçe title","https://example.org/a","Work",true,true,2});
  state.tabs.push_back({"Second","https://example.org/b","",false,false,0});
  state.closed.push_back({"Closed","https://example.org/closed"});
  CHECK(journal.Save(state));CHECK(journal.Load(loaded));
  CHECK(!loaded.clean&&loaded.active==1&&loaded.tabs.size()==2&&loaded.tabs[0].pinned&&loaded.tabs[0].muted&&loaded.tabs[0].group=="Work");
  state.clean=true;CHECK(journal.Save(state));
  std::ofstream(root/L"session-v1.txt",std::ios::trunc)<<"corrupt journal";
  CHECK(journal.Load(loaded));CHECK(!loaded.clean); // verified previous backup
  state.tabs[0].url="file:///C:/Windows/win.ini";CHECK(!journal.Save(state));
  state.tabs[0].url="javascript:alert(1)";CHECK(!journal.Save(state));
  state.tabs[0].url="https://example.org";state.tabs.resize(257);CHECK(!journal.Save(state));
  std::ofstream(root/L"session-v1.bak",std::ios::trunc)<<"TURAN_SESSION 99 1 0\n";
  CHECK(!journal.Load(loaded));
  // Recursive cleanup is confined to a named child of the OS temporary directory.
  CHECK(std::filesystem::equivalent(root.parent_path(),std::filesystem::path(temp)));
  std::error_code error;std::filesystem::remove_all(root,error);
  std::cout<<"PASS session metadata, crash marker, backup recovery, unsafe URL/size/schema rejection\n";
}
