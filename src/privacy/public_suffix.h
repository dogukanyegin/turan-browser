#pragma once
#include <string>
#include <unordered_set>

namespace turan {
// Immutable after construction. ICANN and private PSL rules are both included.
class PublicSuffix {
 public:
  bool Load(const std::string& text,std::string& error);
  std::string Site(const std::string& host) const;
  bool ThirdParty(const std::string& url,const std::string& document) const;
  size_t size() const {return exact_.size()+wildcard_.size()+exception_.size();}
  static std::string Domain(const std::string& value);
 private:
  std::unordered_set<std::string> exact_,wildcard_,exception_;
};
}
