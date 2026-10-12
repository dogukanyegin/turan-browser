#pragma once
#include "privacy/public_suffix.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace turan {
enum class FilterType:uint32_t {Document=1,Subdocument=2,Script=4,Image=8,Stylesheet=16,Font=32,Media=64,Xhr=128,Ping=256,Websocket=512,Object=1024,Other=2048};
enum class FilterCategory {Advertisement,Tracker,Custom};
struct FilterSource {std::string id,text;FilterCategory category=FilterCategory::Custom;};
struct FilterRequest {std::string url,document;FilterType type=FilterType::Other;};
struct FilterDecision {
  bool blocked=false,exception=false;
  std::string rule,source,reason;
  FilterCategory category=FilterCategory::Custom;
};
struct FilterDiagnostic {std::string source,reason;size_t line=0;};
struct FilterReport {
  bool ok=false;
  size_t network=0,cosmetic=0,unsupported=0,invalid=0,disabled=0;
  std::vector<FilterDiagnostic> diagnostics;
  std::string error;
};
// Builds one immutable indexed policy off the UI/request threads. Failed builds
// do not modify a previously published engine. No scriptlet/redirect execution.
class FilterEngine {
 public:
  ~FilterEngine();
  static std::shared_ptr<const FilterEngine> Compile(const std::vector<FilterSource>& sources,std::shared_ptr<const PublicSuffix> suffix,FilterReport& report);
  FilterDecision Check(const FilterRequest& request) const;
  std::vector<std::string> Cosmetics(const std::string& document) const;
 private:
  FilterEngine();
  struct Impl;std::unique_ptr<Impl> impl_;
};
}
