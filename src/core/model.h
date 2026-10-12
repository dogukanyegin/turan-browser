#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <set>
#include "privacy/filter_engine.h"
#include "core/permissions.h"

namespace turan {
std::string Utf8(const std::wstring& value);
std::wstring Wide(const std::string& value);
std::string Host(const std::string& url);
std::string Origin(const std::string& url);
std::string Navigate(const std::string& input, bool https_only);
bool IsThirdParty(const std::string& url, const std::string& first_party);
bool AllowedNavigation(const std::string& url);
struct Entry { std::string title, url; };
struct SavedDownload {
  std::string name,url,path;
  uint64_t bytes=0,finished=0;
  bool canceled=false;
};
struct Settings {
  bool shield = true, third_party_cookies = true, https_only = false, doh = false;
  bool dark = true, turkish = false;
  bool restore_session = true;
};
enum class StoreLoad { Fresh, Loaded, Recovered, Invalid };
class Store {
 public:
  explicit Store(std::filesystem::path root);
  StoreLoad Load();
  bool Save() const;
  bool recovered() const {return recovered_;}
  void Visit(const Entry& entry, bool incognito);
  void Bookmark(const Entry& entry);
  void RememberDownload(const SavedDownload& entry,bool incognito);
  std::filesystem::path root;
  Settings settings;
  std::vector<Entry> bookmarks, history;
  std::vector<SavedDownload> downloads;
  std::vector<PermissionRule> permissions;
 private:
  bool writable_ = true;
  bool recovered_ = false;
  std::vector<std::string> preserved_records_;
};
class Shield {
 public:
  struct Event {std::string url,document;FilterDecision decision;};
  struct Snapshot {uint64_t ads=0,trackers=0,custom=0;std::vector<Event> events;};
  std::atomic<bool> enabled{true}, block_cookies{true}, https_only{false};
  std::atomic<unsigned> blocked{0};
  void LoadRules(const std::filesystem::path& path,const std::filesystem::path& suffix,const std::filesystem::path& profile={});
  bool Block(const std::string& url, const std::string& first_party);
  FilterDecision Check(int browser,const FilterRequest& request,uint64_t generation=0);
  bool ThirdParty(const std::string& url,const std::string& first) const;
  std::vector<std::string> Cosmetics(int browser,const std::string& document) const;
  void Register(int browser,bool incognito);
  void Begin(int browser);
  uint64_t Generation(int browser) const;
  void Forget(int browser);
  Snapshot Events(int browser) const;
  FilterReport Report() const;
  std::shared_ptr<const PublicSuffix> Suffix() const;
  std::string CustomRules() const;
  bool SaveCustom(const std::string& text,std::string& error);
  // Call from a worker: compile a complete immutable policy before publication.
  bool SetSubscriptions(const std::vector<FilterSource>& sources,std::string& error);
  bool SiteEnabled(int browser,const std::string& url) const;
  bool SetSiteEnabled(int browser,const std::string& url,bool enabled,std::string& error);
 private:
  mutable std::mutex mutex_;
  std::shared_ptr<const FilterEngine> engine_;
  std::shared_ptr<const PublicSuffix> suffix_;
  std::vector<FilterSource> starter_;
  std::vector<FilterSource> subscriptions_;
  uint64_t policy_revision_=0;
  FilterReport report_;
  std::filesystem::path profile_;
  std::string custom_;
  std::set<std::string> exceptions_;
  struct BrowserState {bool incognito=true;Snapshot snapshot;std::set<std::string> exceptions;uint64_t generation=1;};
  std::map<int,BrowserState> browsers_;
};
}  // namespace turan
