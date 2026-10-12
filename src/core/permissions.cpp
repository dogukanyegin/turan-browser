#include "core/permissions.h"
#include <ws2tcpip.h>
#include "core/model.h"
#include <algorithm>
#include <cctype>
namespace turan {
bool PermissionOriginValid(const std::string& origin) {
  if(origin.size()>2048||origin.find('\0')!=std::string::npos)return false;
  auto start=origin.find("://");if(start==std::string::npos)return false;
  auto scheme=origin.substr(0,start),authority=origin.substr(start+3);
  if((scheme!="https"&&scheme!="http")||authority.empty()||authority.find_first_of("/?#@\\ \t\r\n")!=std::string::npos)return false;
  if(Origin(origin)!=origin)return false;
  auto host=Host(origin);if(host.empty())return false;
  std::string port;
  if(authority.front()=='[') {
    auto close=authority.find(']');if(close==std::string::npos||close<3)return false;
    IN6_ADDR address;if(InetPtonA(AF_INET6,authority.substr(1,close-1).c_str(),&address)!=1)return false;
    if(close+1<authority.size()){if(authority[close+1]!=':')return false;port=authority.substr(close+2);if(port.empty())return false;}
  } else {
    auto colon=authority.find(':');
    auto name=authority.substr(0,colon);
    if(name.empty()||name.front()=='.'||name.back()=='.'||name.find("..")!=std::string::npos)return false;
    for(unsigned char c:name)if(!(c>='a'&&c<='z')&&!(c>='0'&&c<='9')&&c!='-'&&c!='_'&&c!='.')return false;
    if(colon!=std::string::npos){port=authority.substr(colon+1);if(port.empty())return false;}
  }
  if(!port.empty()) {
    if(port.size()>5)return false;unsigned value=0;
    for(char c:port){if(c<'0'||c>'9')return false;value=value*10+static_cast<unsigned>(c-'0');}
    if(value==0||value>65535)return false;
  }
  return scheme=="https"||host=="localhost"||host=="127.0.0.1"||host=="[::1]";
}
bool PermissionRuleValid(const PermissionRule& rule) {
  return PermissionOriginValid(rule.origin)&&PermissionOriginValid(rule.top)&&rule.kind&&
      !(rule.kind&(rule.kind-1))&&!(rule.kind&~31u);
}
std::optional<bool> PermissionDecision(const std::vector<PermissionRule>& rules,const std::string& origin,const std::string& top,uint32_t kinds) {
  if(!kinds||kinds&~31u||!PermissionOriginValid(origin)||!PermissionOriginValid(top))return {};
  bool missing=false;
  for(uint32_t bit=1;bit<=16;bit<<=1)if(kinds&bit) {
    auto entry=std::find_if(rules.begin(),rules.end(),[&](const PermissionRule& rule){return rule.origin==origin&&rule.top==top&&rule.kind==bit;});
    if(entry==rules.end())missing=true;else if(!entry->allow)return false;
  }
  return missing?std::optional<bool>():std::optional<bool>(true);
}
bool RememberPermission(std::vector<PermissionRule>& rules,const std::string& origin,const std::string& top,uint32_t kinds,bool allow) {
  if(!kinds||kinds&~31u||!PermissionOriginValid(origin)||!PermissionOriginValid(top))return false;
  auto copy=rules;
  for(uint32_t bit=1;bit<=16;bit<<=1)if(kinds&bit) {
    auto entry=std::find_if(copy.begin(),copy.end(),[&](const PermissionRule& rule){return rule.origin==origin&&rule.top==top&&rule.kind==bit;});
    if(entry==copy.end())copy.push_back({origin,top,bit,allow});else entry->allow=allow;
  }
  if(copy.size()>512)return false;rules=std::move(copy);return true;
}
}
