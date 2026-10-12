#include "privacy/public_suffix.h"
#include "core/model.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <algorithm>
#include <sstream>
#include <vector>

namespace turan {
std::string PublicSuffix::Domain(const std::string& value) {
  if(value.empty()||value.size()>1024)return {};
  std::wstring wide(value.size(),0);
  int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),wide.data(),static_cast<int>(wide.size()));
  if(!length)return {};wide.resize(length);
  wchar_t ascii[256]={};length=IdnToAscii(IDN_USE_STD3_ASCII_RULES,wide.data(),static_cast<int>(wide.size()),ascii,256);
  if(!length||length>253)return {};
  std::string domain;for(int i=0;i<length;++i){if(ascii[i]>127)return {};domain.push_back(static_cast<char>(ascii[i]));}
  std::transform(domain.begin(),domain.end(),domain.begin(),[](unsigned char c){return static_cast<char>(c>='A'&&c<='Z'?c+32:c);});
  if(!domain.empty()&&domain.back()=='.')domain.pop_back();
  size_t begin=0;while(begin<domain.size()) {
    auto end=domain.find('.',begin);if(end==std::string::npos)end=domain.size();
    if(end==begin||end-begin>63||domain[begin]=='-'||domain[end-1]=='-')return {};
    begin=end+1;
  }
  return domain;
}
bool PublicSuffix::Load(const std::string& text,std::string& error) {
  if(text.empty()||text.size()>2*1024*1024||text.find('\0')!=std::string::npos){error="Invalid Public Suffix List size/content";return false;}
  PublicSuffix candidate;std::istringstream in(text);std::string line;size_t number=0;
  while(std::getline(in,line)) {
    ++number;if(!line.empty()&&line.back()=='\r')line.pop_back();
    if(line.empty()||line.rfind("//",0)==0)continue;
    auto target=&candidate.exact_;
    if(line.rfind("*.",0)==0){line.erase(0,2);target=&candidate.wildcard_;}
    else if(line[0]=='!'){line.erase(0,1);target=&candidate.exception_;}
    auto domain=Domain(line);
    if(domain.empty()||candidate.size()>=30000){error="Invalid Public Suffix List rule at line "+std::to_string(number);return false;}
    target->insert(domain);
  }
  if(candidate.exact_.empty()){error="Empty Public Suffix List";return false;}
  *this=std::move(candidate);return true;
}
std::string PublicSuffix::Site(const std::string& input) const {
  if(input.empty())return {};
  IN_ADDR v4;IN6_ADDR v6;std::string ip=input;
  if(ip.front()=='['&&ip.back()==']')ip=ip.substr(1,ip.size()-2);
  if(InetPtonA(AF_INET,ip.c_str(),&v4)==1||InetPtonA(AF_INET6,ip.c_str(),&v6)==1)return ip;
  auto host=Domain(input);if(host.empty())return input;
  std::vector<size_t> starts{0};for(size_t i=0;i<host.size();++i)if(host[i]=='.')starts.push_back(i+1);
  size_t suffix=1;
  for(size_t i=0;i<starts.size();++i) {
    auto part=host.substr(starts[i]);const auto labels=starts.size()-i;
    if(exception_.count(part)){suffix=labels-1;break;}
    if(exact_.count(part))suffix=std::max(suffix,labels);
    if(i>0&&wildcard_.count(part))suffix=std::max(suffix,labels+1);
  }
  return host.substr(starts[starts.size()>suffix?starts.size()-suffix-1:0]);
}
bool PublicSuffix::ThirdParty(const std::string& url,const std::string& document) const {
  auto left=Host(url),right=Host(document);return !left.empty()&&!right.empty()&&Site(left)!=Site(right);
}
}
