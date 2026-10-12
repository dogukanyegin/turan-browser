#pragma once
#include <cstddef>
#include <string_view>

namespace turan {
// Move-only, separately allocated memory. The complete allocation is zeroed on
// release; VirtualLock is best effort, not a claim of protection against dumps.
class Secret {
 public:
  Secret()=default;
  explicit Secret(size_t bytes);
  explicit Secret(std::string_view value);
  ~Secret();
  Secret(const Secret&)=delete;
  Secret& operator=(const Secret&)=delete;
  Secret(Secret&&) noexcept;
  Secret& operator=(Secret&&) noexcept;
  unsigned char* data(){return data_;}
  const unsigned char* data() const{return data_;}
  size_t size() const{return size_;}
  bool empty() const{return !size_;}
  std::string_view view() const{return {reinterpret_cast<const char*>(data_),size_};}
  bool valid() const{return !size_||data_!=nullptr;}
  void Clear();
 private:
  unsigned char* data_=nullptr;size_t size_=0;bool locked_=false;
};
bool SecretUtf8Valid(const Secret&,size_t maximum,bool nonempty=true);
Secret SecretFromWide(std::wstring_view value);
}
