#pragma once
#include "security/secret.h"
#include <filesystem>
#include <mutex>
#include <vector>
#include <array>
#include <string>

namespace turan {
struct CredentialInfo {std::string id,origin,username;uint64_t updated=0;};
class PasswordVault {
 public:
  explicit PasswordVault(std::filesystem::path profile);
  bool Exists() const;
  bool Create(const Secret& passphrase,std::string& error);
  bool Unlock(const Secret& passphrase,std::string& error);
  bool Reauthenticate(const Secret& passphrase,std::string& error) const;
  void Lock();
  bool Unlocked() const;
  std::vector<CredentialInfo> List(const std::string& search="") const;
  bool Save(const std::string& origin,const std::string& username,const Secret& password,std::string& error);
  bool Remove(const std::string& id,std::string& error);
  Secret Reveal(const std::string& id,const std::string& exact_origin,std::string& error) const;
  bool ChangePassphrase(const Secret& passphrase,std::string& error);
  bool Export(const std::filesystem::path& output,const Secret& export_passphrase,std::string& error) const;
  bool Import(const std::filesystem::path& input,const Secret& export_passphrase,std::string& error,size_t& imported);
  static Secret Generate(size_t length=24);
  static bool PassphraseValid(const Secret&);
  static bool OriginValid(const std::string&);
 private:
  struct Record {CredentialInfo info;std::string cipher;};
  bool Publish(const std::vector<Record>& next,std::string& error);
  bool Open(const std::string& disk,const Secret& passphrase,std::vector<Record>& records,Secret& key,std::array<unsigned char,16>& salt,std::string& error) const;
  std::filesystem::path root_,file_;
  mutable std::mutex mutex_;
  Secret key_;
  std::array<unsigned char,16> salt_={};
  std::vector<Record> records_;
  std::string disk_;
};
}
