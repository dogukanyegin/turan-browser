#pragma once
#include "core/profile_backup.h"
namespace turan {
void ReportProfileOperation(const ProfileOperation& operation);
// Starts a small engine-free utility with only an inherited process handle. It
// waits for this browser to exit before reading files such as Chromium's log.
bool StartProfileBackupWorker(const std::filesystem::path& directory,const std::filesystem::path& profile,
                             const std::filesystem::path& destination,std::string& error);
}
