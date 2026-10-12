#pragma once
#include "update/updater.h"
#include <windows.h>
#include <memory>
#include <utility>

namespace turan::update::detail {
struct Handle {
  HANDLE h=INVALID_HANDLE_VALUE;
  Handle()=default;
  explicit Handle(HANDLE value):h(value){}
  Handle(const Handle&)=delete;
  Handle& operator=(const Handle&)=delete;
  Handle(Handle&& other) noexcept:h(std::exchange(other.h,INVALID_HANDLE_VALUE)){}
  Handle& operator=(Handle&& other) noexcept {Close();h=std::exchange(other.h,INVALID_HANDLE_VALUE);return *this;}
  ~Handle(){Close();}
  void Close(){if(h!=INVALID_HANDLE_VALUE&&h)CloseHandle(h);h=INVALID_HANDLE_VALUE;}
  explicit operator bool() const{return h!=INVALID_HANDLE_VALUE&&h;}
};
bool Normalize(const std::filesystem::path& input,std::filesystem::path& output,std::string& error);
bool Plain(const std::filesystem::path& path,bool directory);
bool Missing(const std::filesystem::path& path);
bool Read(const std::filesystem::path& path,uint64_t limit,std::string& bytes);
bool Write(const std::filesystem::path& path,const std::string& bytes,bool replace=false);
Handle Open(const std::filesystem::path& path,bool rename=false);
bool Hash(HANDLE file,uint64_t& size,std::string& digest);
bool Rename(HANDLE file,const std::filesystem::path& destination);
bool Match(const std::filesystem::path& path,const File& file);
bool Parents(const std::filesystem::path& path,std::vector<Handle>& guards,bool create=false);
std::string RootId(const std::filesystem::path& root);
std::string RandomId();
uint64_t Now();
bool Fresh(const Policy& policy,const Manifest& manifest,uint64_t previous,std::string& error);
bool ParseManifest(const std::string& bytes,Manifest& manifest,std::string& error);
bool Receipt(const std::filesystem::path& app,const Policy& policy,Manifest& prior,bool& present,std::string& error);
}
