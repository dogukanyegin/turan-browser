#pragma once
#include "privacy/filter_engine.h"
#include <array>
#include <atomic>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>

namespace turan {
struct FilterSubscriptionDefinition {
  std::string id,name,url,license;
  FilterCategory category;
};
struct FilterSubscriptionStatus {
  std::string id,name,url,license,sha256,error;
  bool enabled=false,cached=false,updating=false,recovered=false;
  uint64_t updated=0,next_attempt=0;
  size_t bytes=0,network=0,cosmetic=0,unsupported=0,invalid=0;
};
// Optional subscriptions are disabled until explicit native user consent.
// HTTPS is authenticated by Windows; hashes detect cache corruption, not
// publisher signatures. Unsupported rules remain skipped and reported.
class FilterSubscriptions {
 public:
  using Fetch=std::function<bool(const std::string&,std::string&,std::string&)>;
  static const std::array<FilterSubscriptionDefinition,2>& Catalog();
  static bool Download(const std::string& url,std::string& body,std::string& error);
  FilterSubscriptions(std::filesystem::path profile,std::shared_ptr<const PublicSuffix> suffix,Fetch fetch={});
  ~FilterSubscriptions();
  FilterSubscriptions(const FilterSubscriptions&)=delete;
  FilterSubscriptions& operator=(const FilterSubscriptions&)=delete;
  bool SetEnabled(const std::string& id,bool enabled,bool explicit_network_consent,std::string& error);
  // Non-blocking. Call from the native timer; network/parsing work runs on one
  // worker. Failed updates back off one hour and keep the validated old policy.
  bool RefreshAsync(bool force=false,uint64_t now=0);
  bool Busy() const;
  uint64_t Revision() const;
  std::vector<FilterSubscriptionStatus> Snapshot() const;
  std::vector<FilterSource> Sources() const;
  bool Rollback(const std::string& id,std::string& error);
 private:
  struct Item {FilterSubscriptionStatus status;std::string text;};
  bool Validate(const std::string& text,const FilterSubscriptionDefinition& definition,FilterReport& report,std::string& error) const;
  bool ReadCache(size_t index,const wchar_t* suffix,Item& item) const;
  void Refresh(bool force,uint64_t now);
  std::filesystem::path profile_;
  std::shared_ptr<const PublicSuffix> suffix_;
  Fetch fetch_;
  mutable std::mutex mutex_;
  std::array<Item,2> items_;
  std::future<void> worker_;
  std::atomic<bool> stopping_{false};
  bool safe_=false,busy_=false;
  uint64_t revision_=1;
};
}
