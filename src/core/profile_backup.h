#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace turan {
struct ProfileLimits {
  uint64_t bytes=20ULL*1024*1024*1024;
  size_t entries=100000;
  unsigned depth=64;
};
struct ProfileBackupInfo {
  std::string engine;
  uint64_t created=0,bytes=0,files=0;
};
struct ProfileOperation {
  bool ok=false;
  std::string error;
  std::filesystem::path backup,previous;
  ProfileBackupInfo info;
};
// An exclusive sibling lease stays open while the profile directory is renamed.
// Windows may refuse directory moves while any descendant file is open.
class ProfileLease {
 public:
  ProfileLease()=default;
  ~ProfileLease();
  ProfileLease(const ProfileLease&)=delete;
  ProfileLease& operator=(const ProfileLease&)=delete;
  bool Acquire(const std::filesystem::path& root,std::string& error,bool create=true);
  void Release();
  bool Owns(const std::filesystem::path& root) const;
 private:
  void* handle_=nullptr;
  std::filesystem::path root_;
};
using ProfileProgress=std::function<bool(uint64_t,uint64_t)>;
bool ReadProfileEngine(const std::filesystem::path& root,std::string& engine,std::string& error);
// Record a conservative engine bound before CEF can mutate the cache. The
// attempt survives failed initialization or a failed successful-marker write.
bool BeginProfileEngine(const std::filesystem::path& root,const std::string& engine,ProfileLease& lease);
bool MarkProfileEngine(const std::filesystem::path& root,const std::string& engine);
// Resolve an interrupted directory handoff before creating/opening a profile.
// Uninstalled staging copies are retained; ambiguous states stop startup.
bool RecoverProfileRestore(const std::filesystem::path& root,std::string& error);
ProfileOperation VerifyProfileBackup(const std::filesystem::path& backup,ProfileLimits limits={},ProfileProgress progress={});
ProfileOperation CreateProfileBackup(const std::filesystem::path& root,const std::filesystem::path& destination,
                                    ProfileLease& lease,ProfileLimits limits={},ProfileProgress progress={});
ProfileOperation RestoreProfileBackup(const std::filesystem::path& root,const std::filesystem::path& backup,
                                     ProfileLease& lease,ProfileLimits limits={},ProfileProgress progress={});
// Take a complete closed-profile snapshot before an engine change. A newer
// successful or attempted engine is refused; an attempt is recorded separately
// before CEF starts, and success commits its final marker.
ProfileOperation PrepareProfileEngine(const std::filesystem::path& root,const std::string& engine,ProfileLease& lease);
std::filesystem::path NextProfileBackup(const std::filesystem::path& root);
}
