#include "security/secret.h"
#include <windows.h>
#include <utility>
#include <cstring>
namespace turan {
Secret::Secret(size_t bytes):size_(bytes) {
  if(!bytes)return;
  data_=static_cast<unsigned char*>(VirtualAlloc(nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
  if(data_)locked_=VirtualLock(data_,bytes)!=FALSE;
}
Secret::Secret(std::string_view value):Secret(value.size()){if(data_)memcpy(data_,value.data(),size_);}
Secret::~Secret(){Clear();}
Secret::Secret(Secret&& other) noexcept:data_(std::exchange(other.data_,nullptr)),size_(std::exchange(other.size_,0)),locked_(std::exchange(other.locked_,false)){}
Secret& Secret::operator=(Secret&& other) noexcept {if(this!=&other){Clear();data_=std::exchange(other.data_,nullptr);size_=std::exchange(other.size_,0);locked_=std::exchange(other.locked_,false);}return *this;}
void Secret::Clear() {
  if(data_){SecureZeroMemory(data_,size_);if(locked_)VirtualUnlock(data_,size_);VirtualFree(data_,0,MEM_RELEASE);}
  data_=nullptr;size_=0;locked_=false;
}
bool SecretUtf8Valid(const Secret& value,size_t maximum,bool nonempty) {
  if(!value.valid()||value.size()>maximum||(nonempty&&value.empty()))return false;
  if(value.empty())return true;
  for(auto ch:value.view())if(ch=='\0')return false;
  return MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.view().data(),static_cast<int>(value.size()),nullptr,0)>0;
}
Secret SecretFromWide(std::wstring_view value) {
  if(value.empty())return {};
  int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
  if(size<=0)return {};Secret result(static_cast<size_t>(size));
  if(!result.valid()||WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),reinterpret_cast<char*>(result.data()),size,nullptr,nullptr)!=size)return {};
  return result;
}
}
