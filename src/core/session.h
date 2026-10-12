#pragma once
#include "core/model.h"

namespace turan {
struct SessionTab {
  std::string title,url,group;
  bool pinned=false,muted=false;
  unsigned color=0;
};
struct SessionState {
  bool clean=true;
  unsigned active=0;
  std::vector<SessionTab> tabs,closed;
};
// Separate versioned journal: never changes the legacy bookmark/history format.
class SessionJournal {
 public:
  explicit SessionJournal(std::filesystem::path root):root_(std::move(root)){}
  bool Load(SessionState& state) const;
  bool Save(const SessionState& state) const;
 private:
  std::filesystem::path root_;
};
}
