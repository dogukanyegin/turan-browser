#pragma once
#include <windows.h>
#include <filesystem>
#include <functional>

namespace turan {
// Native privileged UI. No page script, URL query or web message can invoke
// update checks or installation. Preparation callback runs on the UI thread.
using UpdatePrepared=std::function<void(const std::filesystem::path&)>;
HWND ShowUpdateWindow(HWND owner,const std::filesystem::path& app,bool turkish,
                      UpdatePrepared prepared);
// Required after the CEF message loop and before CEF shutdown / DLL unload.
// Cancels preparation and joins every owned native update worker.
void WaitForUpdateWorkers();
}
