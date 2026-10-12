#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>

namespace turan {
struct TorBrowserResult {
  bool ok=false;
  uint32_t process_id=0;
  size_t verified_files=0;
  std::string error;
};
// No network calls, shortcuts, profile arguments, shell, or data import.
bool TorTargetUrl(const std::string& url,std::wstring& target,std::string& error);
std::wstring QuoteProcessArgument(const std::wstring& argument);
std::vector<std::filesystem::path> TorBrowserCandidates(const std::filesystem::path& profile);
std::filesystem::path TorBrowserPreference(const std::filesystem::path& profile);
bool SaveTorBrowserPreference(const std::filesystem::path& profile,const std::filesystem::path& root);
TorBrowserResult VerifyTorBrowser(const std::filesystem::path& root);
TorBrowserResult OpenInTorBrowser(const std::filesystem::path& root,const std::string& url);
const char* VerifiedTorBrowserVersion();
}
