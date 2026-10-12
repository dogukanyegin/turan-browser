#include <windows.h>
#include <shlobj.h>
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_sandbox_win.h"
#include "include/cef_browser_process_handler.h"
#include "browser/network.h"
#include "core/model.h"
#include "core/state_file.h"
#include "core/profile_backup.h"
#include "core/profile_worker.h"
#include "ui/window.h"
#include "ui/update_window.h"
#include "update/updater.h"
#include "turan_version.h"

namespace {
class App final : public CefApp,public CefBrowserProcessHandler {
 public:
  explicit App(bool chrome_probe):chrome_probe_(chrome_probe){}
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {return chrome_probe_?this:nullptr;}
  CefRefPtr<CefClient> GetDefaultClient() override {return owner_?owner_->EngineClient():nullptr;}
  void SetOwner(turan::Window* owner){owner_=owner;}
  void OnBeforeCommandLineProcessing(const CefString&,CefRefPtr<CefCommandLine> command) override {
    command->AppendSwitch("disable-background-networking");
    command->AppendSwitch("disable-component-update");
    command->AppendSwitch("disable-sync");
    if(!chrome_probe_)command->AppendSwitch("disable-extensions");
    command->AppendSwitch("disable-domain-reliability");
    command->AppendSwitch("disable-default-apps");
    auto rules=command->GetSwitchValue("host-resolver-rules").ToString();
    if(!rules.empty())rules+=", ";
    rules+="MAP android.clients.google.com ~NOTFOUND, MAP mtalk.google.com ~NOTFOUND, MAP optimizationguide-pa.googleapis.com ~NOTFOUND, MAP update.googleapis.com ~NOTFOUND, MAP clients2.google.com ~NOTFOUND, MAP clients4.google.com ~NOTFOUND";
    command->AppendSwitchWithValue("host-resolver-rules",rules);
    // Secure DNS is configured through Chromium's global preference service,
    // not unrecognized command-line switches.
  }
 private: bool chrome_probe_;turan::Window* owner_=nullptr; IMPLEMENT_REFCOUNTING(App);
};
}
CEF_BOOTSTRAP_EXPORT int RunWinMain(HINSTANCE instance,LPTSTR,int,void* sandbox,cef_version_info_t*) {
  const auto startup_tick=GetTickCount64();
  CefMainArgs args(instance);
  int code=CefExecuteProcess(args,nullptr,sandbox);if(code>=0)return code;
  if(!sandbox){MessageBoxW(nullptr,L"The CEF sandbox bootstrap is required.",L"Turan Browser",MB_ICONERROR);return 1;}
  wchar_t exe[32768];GetModuleFileNameW(nullptr,exe,32768);auto app_dir=std::filesystem::path(exe).parent_path();
  std::string update_error;
  if(!turan::update::Recover(app_dir,turan::update::PublisherPolicy(),update_error)) {
    MessageBoxW(nullptr,turan::Wide(update_error).c_str(),L"Turan Browser - update recovery",MB_OK|MB_ICONERROR);return 7;
  }
  auto command=CefCommandLine::CreateCommandLine();command->InitFromString(GetCommandLineW());
  const auto runtime=command->GetSwitchValue("turan-runtime").ToString();
  bool reparent_chrome=false;
  if(runtime=="chrome-reparent-tabs") {
#ifdef TURAN_ENABLE_NATIVE_CHROME_PROBE
    reparent_chrome=true;
#else
    return 2;
#endif
  }
  const bool integrated_chrome=runtime=="chrome-integrated";
  const bool chrome_probe=runtime=="chrome"||runtime=="chrome-tabs"||runtime=="chrome-native-tabs"||reparent_chrome||integrated_chrome;
  // Validate probes before any lease, recovery, metadata or engine operation.
  if(command->HasSwitch("turan-runtime")&&!chrome_probe)return 2;
  if(chrome_probe&&!command->HasSwitch("profile"))return 2;
  const bool test_dialog=command->HasSwitch("turan-test-file-dialog");
  const bool test_dns=command->HasSwitch("turan-test-dns");
  const bool test_startup=command->HasSwitch("turan-test-startup");
  if((command->HasSwitch("smoke-result")||test_dialog||test_dns||test_startup)&&
     (!command->HasSwitch("profile")||turan::Host(command->GetSwitchValue("url"))!="127.0.0.1"))return 2;
  std::filesystem::path profile;
  if(command->HasSwitch("profile"))profile=turan::Wide(command->GetSwitchValue("profile"));
  else if(std::filesystem::exists(app_dir/L"portable.flag"))profile=app_dir/L"Profile";
  else {PWSTR local=nullptr;SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local);if(!local)return 1;profile=std::filesystem::path(local)/TURAN_PROFILE_NAMESPACE/L"Profile";CoTaskMemFree(local);}
  std::error_code ec;profile=std::filesystem::absolute(profile,ec);if(ec)return 1;
  const bool backup_profile=command->HasSwitch("backup-profile"),restore_profile=command->HasSwitch("restore-profile");
  if((backup_profile||restore_profile)&&(!command->HasSwitch("profile")||backup_profile==restore_profile)) {
    MessageBoxW(nullptr,L"Use an explicit --profile path and exactly one of --backup-profile=<new directory> or --restore-profile=<verified snapshot>.",L"Turan Browser - profile operation",MB_OK|MB_ICONERROR);return 2;
  }
  std::string profile_error;
  if(!turan::RecoverProfileRestore(profile,profile_error)) {
    MessageBoxW(nullptr,turan::Wide(profile_error).c_str(),L"Turan Browser - profile recovery",MB_OK|MB_ICONERROR);return 6;
  }
  turan::ProfileLease profile_lease;
  if(!profile_lease.Acquire(profile,profile_error,!backup_profile&&!restore_profile)) {
    MessageBoxW(nullptr,turan::Wide(profile_error).c_str(),L"Turan Browser - profile in use",MB_OK|MB_ICONERROR);return 6;
  }
  // Closed-profile utilities intentionally run before Store parsing or CEF startup:
  // a damaged metadata file can still be backed up byte for byte.
  if(backup_profile||restore_profile) {
    auto destination=turan::Wide(command->GetSwitchValue(backup_profile?"backup-profile":"restore-profile"));
    if(destination.empty())return 2;
    auto result=backup_profile?turan::CreateProfileBackup(profile,destination,profile_lease):turan::RestoreProfileBackup(profile,destination,profile_lease);
    turan::ReportProfileOperation(result);return result.ok?0:6;
  }
  turan::Store store(profile);auto loaded=store.Load();
  if(loaded==turan::StoreLoad::Invalid) {
    MessageBoxW(nullptr,L"Profile settings, history or bookmarks could not be validated, and no valid backup was found.\n\nThe existing files have been preserved. Close Turan and restore browser-state.txt from your own backup before reopening this profile.",L"Turan Browser - profile recovery",MB_OK|MB_ICONERROR);
    return 3;
  }
  CefSettings settings;settings.no_sandbox=false;settings.persist_session_cookies=false;
  CefString(&settings.root_cache_path)=profile.wstring();CefString(&settings.cache_path)=(profile/L"Cache").wstring();
  CefString(&settings.locale)=store.settings.turkish?"tr":"en-US";
  CefString(&settings.log_file)=(profile/L"cef.log").wstring();settings.log_severity=LOGSEVERITY_WARNING;
  auto prepared=turan::PrepareProfileEngine(profile,TURAN_ENGINE_VERSION,profile_lease);
  if(!prepared.ok){turan::ReportProfileOperation(prepared);return 6;}
  if(!turan::BeginProfileEngine(profile,TURAN_ENGINE_VERSION,profile_lease)) {
    turan::ProfileOperation failed;failed.error="The pre-start engine marker could not be saved. CEF has not been initialized; the existing profile is preserved.";
    turan::ReportProfileOperation(failed);return 6;
  }
  CefRefPtr<App> app=new App(chrome_probe);
  const auto before_initialize=GetTickCount64();
  if(!CefInitialize(args,settings,app,sandbox))return CefGetExitCode();
  const auto initialized=GetTickCount64();
  bool backup_on_close=false;
  {
    if(!turan::MarkProfileEngine(profile,TURAN_ENGINE_VERSION)) {
      MessageBoxW(nullptr,L"The engine started, but its profile version marker could not be saved. No browsing window has been opened. Check profile permissions before restarting.",L"Turan Browser - profile version",MB_OK|MB_ICONERROR);code=6;
    } else if(!turan::ConfigureDns(store.settings.doh,test_dns?profile/L"network-configuration.txt":std::filesystem::path())) {
      MessageBoxW(nullptr,L"The selected DNS configuration could not be applied. No browsing window has been opened. Check the engine's managed policies and DNS preferences.",L"Turan Browser - DNS configuration",MB_OK|MB_ICONERROR);code=4;
    } else {
      const auto dns_configured=GetTickCount64();
      auto smoke=turan::Wide(command->GetSwitchValue("smoke-result"));
      turan::Window window(store,app_dir,command->GetSwitchValue("url"),smoke,chrome_probe,runtime=="chrome-tabs"||runtime=="chrome-native-tabs"||reparent_chrome||integrated_chrome,runtime=="chrome-native-tabs"||reparent_chrome||integrated_chrome,test_dialog,reparent_chrome,integrated_chrome);
      app->SetOwner(&window);
      if(window.Create(instance)) {
        if(test_startup)turan::WriteStateFile(profile/L"startup-timing.txt",
          "beforeInitializeMs "+std::to_string(before_initialize-startup_tick)+
          "\ninitializeMs "+std::to_string(initialized-before_initialize)+
          "\ndnsMs "+std::to_string(dns_configured-initialized)+
          "\nwindowMs "+std::to_string(GetTickCount64()-dns_configured)+"\n");
        CefRunMessageLoop();
        turan::WaitForUpdateWorkers();
        backup_on_close=window.BackupRequested();
      } else code=1;
      app->SetOwner(nullptr);
    }
  }
  CefShutdown();
  if(backup_on_close&&!turan::StartProfileBackupWorker(app_dir,profile,turan::NextProfileBackup(profile),profile_error)) {
    turan::ProfileOperation failed;failed.error=profile_error;turan::ReportProfileOperation(failed);code=6;
  }
  return code<0?0:code;
}
