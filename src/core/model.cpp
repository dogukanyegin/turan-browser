#include "core/model.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace turan {
std::string Utf8(const std::wstring& s) {
  if(s.empty()) return {};
  int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
  std::string out(n,0); WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr); return out;
}
std::wstring Wide(const std::string& s) {
  if(s.empty()) return {};
  int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);
  std::wstring out(n,0); MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n); return out;
}
static std::string Lower(std::string s) { for(char& c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s; }
std::string Host(const std::string& url) {
  auto start=url.find("://"); if(start==std::string::npos)return {};
  start+=3; auto end=url.find_first_of("/?#",start);
  std::string authority=url.substr(start,end==std::string::npos?end:end-start);
  auto at=authority.rfind('@');if(at!=std::string::npos)authority.erase(0,at+1);
  if(!authority.empty() && authority[0]=='[') { auto close=authority.find(']');return Lower(authority.substr(0,close+1)); }
  auto port=authority.find(':'); if(port!=std::string::npos)authority.resize(port);
  while(!authority.empty() && authority.back()=='.')authority.pop_back();
  return Lower(authority);
}
std::string Origin(const std::string& url) {
  auto end=url.find("://"); if(end==std::string::npos)return {};
  auto stop=url.find_first_of("/?#",end+3);return Lower(url.substr(0,stop));
}
static bool Related(const std::string& a,const std::string& b) {
  return a==b || (a.size()>b.size() && a.compare(a.size()-b.size(),b.size(),b)==0 && a[a.size()-b.size()-1]=='.');
}
bool IsThirdParty(const std::string& url,const std::string& first) {
  auto a=Host(url),b=Host(first);return !a.empty()&&!b.empty()&&!Related(a,b)&&!Related(b,a);
}
bool AllowedNavigation(const std::string& url) {
  auto s=Lower(url);return s.rfind("https://",0)==0||s.rfind("http://",0)==0||s=="about:blank";
}
std::string Navigate(const std::string& input,bool https_only) {
  auto begin=input.find_first_not_of(" \t\r\n"),end=input.find_last_not_of(" \t\r\n");
  if(begin==std::string::npos)return "about:blank";
  std::string s=input.substr(begin,end-begin+1),low=Lower(s);
  if(low.rfind("http://",0)==0||low.rfind("https://",0)==0) {
    if(https_only && low.rfind("http://",0)==0 && Host(s)!="localhost" && Host(s)!="127.0.0.1")s.replace(0,4,"https");
    return s;
  }
  if(s=="about:blank")return s;
  if(s.find(":")!=std::string::npos && s.find("://")!=std::string::npos)return "about:blank";
  if(s.find(' ')==std::string::npos && (s.find('.')!=std::string::npos||s.rfind("localhost",0)==0))return "https://"+s;
  std::ostringstream escaped; escaped<<std::hex<<std::uppercase;
  for(unsigned char c:s) { if(std::isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~')escaped<<c;else escaped<<'%'<<std::setw(2)<<std::setfill('0')<<static_cast<unsigned>(c); }
  return "https://duckduckgo.com/?q="+escaped.str();
}
}
