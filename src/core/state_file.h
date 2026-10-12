#pragma once
#include <filesystem>
#include <string>

namespace turan {
enum class StateRead { Missing, Read, Error };
// Profile-owned leaf files only. Reparse-point leaves are refused.
StateRead ReadStateFile(const std::filesystem::path& path,std::string& bytes,size_t limit);
bool WriteStateFile(const std::filesystem::path& path,const std::string& bytes);
bool QuarantineStateFile(const std::filesystem::path& path);
}
