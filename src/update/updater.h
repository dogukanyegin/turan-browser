#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace turan::update {
struct Version {
  uint32_t major=0,minor=0,patch=0,revision=0;
  bool operator<(const Version& other) const;
  bool operator==(const Version& other) const;
};
struct File {
  std::string path,sha256;
  uint64_t bytes=0;
};
struct Manifest {
  Version version;
  uint64_t sequence=0,issued=0,expires=0;
  std::string channel,kind,base_url;
  std::vector<File> files;
};
// The public key and metadata URL are compiled into the application/helper.
// Never load a replacement trust root from a downloaded manifest or profile.
struct Policy {
  std::vector<unsigned char> rsa_public_blob;
  std::string manifest_url,channel;
  // Compiled from the release's explicit CMake runtime inventory. A signed
  // offer cannot add arbitrary files below resources/docs or overwrite data.
  std::vector<std::string> runtime_paths;
  Version running_version;
  uint64_t running_sequence=0;
};
struct Offer {
  Manifest manifest;
  std::string manifest_bytes,signature;
};
Policy PublisherPolicy();
bool Configured(const Policy& policy);
bool RuntimePathValid(const std::string& path);
bool HttpsUrlValid(const std::string& url);
bool Authenticate(const Policy& policy,const std::string& bytes,const std::string& signature,
                  Manifest& manifest,std::string& error,bool recovery=false);
using Cancelled=std::function<bool()>;
bool Check(const std::filesystem::path& app,const Policy& policy,Offer& offer,std::string& error,const Cancelled& cancelled={});
// The injected fetch callback is a native test seam. Production uses WinHTTP.
using Fetch=std::function<bool(const std::string&,const std::filesystem::path&,uint64_t,std::string&)>;
bool Stage(const std::filesystem::path& app,const Policy& policy,const Offer& offer,
           std::filesystem::path& staging,std::string& error,const Fetch& fetch={},const Cancelled& cancelled={});
bool Apply(const std::filesystem::path& app,const std::filesystem::path& staging,
           const Policy& policy,std::string& error);
// Called before CEF starts. Interrupted replacements are restored from their
// verified backups. Ambiguous/tampered files stop recovery without deletion.
bool Recover(const std::filesystem::path& app,const Policy& policy,std::string& error);
bool Download(const std::string& url,const std::filesystem::path& target,uint64_t limit,std::string& error,const Cancelled& cancelled={});
bool DownloadBytes(const std::string& url,uint64_t limit,std::string& bytes,std::string& error,const Cancelled& cancelled={});
std::string Sha256(const std::string& bytes);
// Test-only injected crash/failure point. Not exposed to browser command lines.
using InstallHook=std::function<bool(size_t)>;
bool ApplyForTest(const std::filesystem::path& app,const std::filesystem::path& staging,
                  const Policy& policy,std::string& error,const InstallHook& hook);
}
