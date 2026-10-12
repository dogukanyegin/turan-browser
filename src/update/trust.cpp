#include "update/updater.h"
#if __has_include("turan_version.h")
#include "turan_version.h"
#endif

#ifdef TURAN_UPDATE_INVENTORY_HEADER
#include TURAN_UPDATE_INVENTORY_HEADER
#endif

#ifdef TURAN_UPDATE_TRUST_HEADER
#include TURAN_UPDATE_TRUST_HEADER
#else
// Unconfigured builds deliberately have no trust root or network endpoint.
// Publisher provisioning produces an external build header, never a profile
// preference. Test signing keys must not be used here.
namespace turan::update::configured {
constexpr char public_key_hex[]="";
constexpr char manifest_url[]="";
#ifdef TURAN_RELEASE_CHANNEL
constexpr char channel[]=TURAN_RELEASE_CHANNEL;
#else
constexpr char channel[]="development";
#endif
constexpr unsigned long long sequence=0;
}
#endif

namespace turan::update {
Policy PublisherPolicy() {
  Policy p;p.manifest_url=configured::manifest_url;p.channel=configured::channel;p.running_sequence=configured::sequence;
#ifdef TURAN_UPDATE_INVENTORY_HEADER
  for(const auto* path:inventory::paths)p.runtime_paths.emplace_back(path);
#endif
#ifdef TURAN_FILE_VERSION
  p.running_version={TURAN_FILE_VERSION};
#else
  p.running_version={0,2,0,0};
#endif
  std::string hex=configured::public_key_hex;
  if(hex.size()%2||hex.find_first_not_of("0123456789abcdef")!=std::string::npos)return p;
  auto n=[](char c){return c<='9'?c-'0':c-'a'+10;};
  for(size_t i=0;i<hex.size();i+=2)p.rsa_public_blob.push_back(static_cast<unsigned char>(n(hex[i])*16+n(hex[i+1])));
  return p;
}
}
