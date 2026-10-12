#pragma once
#include <filesystem>
#include <string>

namespace turan::update {
// Copy the currently shipped CEF-free helper into the owned staging directory,
// validate its bytes, then start it with the exact parent process identity.
// The caller closes Turan after success; the helper waits for that process and
// asks for native user confirmation. No downloaded executable is launched.
bool StartWorker(const std::filesystem::path& app,const std::filesystem::path& stage,std::string& error);
}
