#include "browser/network.h"
#include "core/state_file.h"
#include "include/cef_preference.h"
#include <sstream>
namespace turan {
bool ConfigureDns(bool secure,const std::filesystem::path& diagnostic) {
  auto manager=CefPreferenceManager::GetGlobalPreferenceManager();if(!manager)return false;
  auto value=CefValue::Create();CefString error;bool ok=true;
  auto set=[&](const char* name,const char* setting) {
    value->SetString(setting);if(!manager->SetPreference(name,value,error))ok=false;
  };
  if(secure) {
    value->SetBool(true);ok=manager->SetPreference("async_dns.enabled",value,error);
    set("dns_over_https.templates","https://cloudflare-dns.com/dns-query");set("dns_over_https.mode","secure");
  } else {set("dns_over_https.mode","off");set("dns_over_https.templates","");}
  auto mode=manager->GetPreference("dns_over_https.mode"),templates=manager->GetPreference("dns_over_https.templates");
  ok=ok&&mode&&templates&&mode->GetString()==(secure?"secure":"off")&&
      templates->GetString()==(secure?"https://cloudflare-dns.com/dns-query":"");
  if(!diagnostic.empty()) {
    std::ostringstream output;output<<"APPLIED "<<ok<<"\nMODE "<<(mode?mode->GetString().ToString():"unavailable")<<
        "\nTEMPLATES "<<(templates?templates->GetString().ToString():"unavailable")<<'\n';
    WriteStateFile(diagnostic,output.str());
  }
  return ok;
}
}
