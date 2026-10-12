#include "update/internal.h"
#include <bcrypt.h>
#include <algorithm>
#include <charconv>
#include <cstring>
#include <set>
#include <sstream>
#include <tuple>

namespace turan::update {
bool Version::operator<(const Version& o) const{return std::tie(major,minor,patch,revision)<std::tie(o.major,o.minor,o.patch,o.revision);}
bool Version::operator==(const Version& o) const{return std::tie(major,minor,patch,revision)==std::tie(o.major,o.minor,o.patch,o.revision);}
namespace {
bool Number(const std::string& s,uint64_t& n,uint64_t max=UINT64_MAX) {
  if(s.empty()||s.size()>20||(s.size()>1&&s[0]=='0'))return false;
  auto result=std::from_chars(s.data(),s.data()+s.size(),n);
  return result.ec==std::errc()&&result.ptr==s.data()+s.size()&&n<=max;
}
bool Hex(const std::string& s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
std::string Lower(std::string s){for(auto& c:s)if(c>='A'&&c<='Z')c=static_cast<char>(c+32);return s;}
std::vector<std::string> Split(const std::string& s,char separator) {
  std::vector<std::string> result;size_t start=0;
  for(;;){auto end=s.find(separator,start);result.push_back(s.substr(start,end-start));if(end==std::string::npos)break;start=end+1;}return result;
}
bool PublicKey(const std::vector<unsigned char>& bytes) {
  if(bytes.size()<sizeof(BCRYPT_RSAKEY_BLOB))return false;
  BCRYPT_RSAKEY_BLOB b={};memcpy(&b,bytes.data(),sizeof b);
  return b.Magic==BCRYPT_RSAPUBLIC_MAGIC&&(b.BitLength==3072||b.BitLength==4096)&&
      b.cbModulus==b.BitLength/8&&b.cbPublicExp==3&&!b.cbPrime1&&!b.cbPrime2&&
      bytes.size()==sizeof b+b.cbPublicExp+b.cbModulus&&
      bytes[sizeof b]==1&&bytes[sizeof b+1]==0&&bytes[sizeof b+2]==1&&
      (bytes[sizeof b+3]&0x80)!=0&&bytes.back()%2==1;
}
bool Verify(const Policy& policy,const std::string& bytes,const std::string& signature) {
  if(!PublicKey(policy.rsa_public_blob)||signature.size()!=policy.rsa_public_blob.size()-sizeof(BCRYPT_RSAKEY_BLOB)-3)return false;
  BCRYPT_ALG_HANDLE provider=nullptr;BCRYPT_KEY_HANDLE key=nullptr;
  if(BCryptOpenAlgorithmProvider(&provider,BCRYPT_RSA_ALGORITHM,nullptr,0)<0)return false;
  bool ok=BCryptImportKeyPair(provider,nullptr,BCRYPT_RSAPUBLIC_BLOB,&key,
      const_cast<PUCHAR>(policy.rsa_public_blob.data()),static_cast<ULONG>(policy.rsa_public_blob.size()),0)>=0;
  auto hex=Sha256(bytes);unsigned char hash[32]={};
  if(hex.size()!=64)ok=false;
  if(ok)for(size_t i=0;i<32;++i){auto n=[](char c){return c<='9'?c-'0':c-'a'+10;};hash[i]=static_cast<unsigned char>(n(hex[2*i])*16+n(hex[2*i+1]));}
  BCRYPT_PSS_PADDING_INFO padding={BCRYPT_SHA256_ALGORITHM,32};
  if(ok)ok=BCryptVerifySignature(key,&padding,hash,32,reinterpret_cast<PUCHAR>(const_cast<char*>(signature.data())),
      static_cast<ULONG>(signature.size()),BCRYPT_PAD_PSS)>=0;
  if(key)BCryptDestroyKey(key);BCryptCloseAlgorithmProvider(provider,0);return ok;
}
}
bool RuntimePathValid(const std::string& name) {
  if(name.empty()||name.size()>240||name.front()=='/'||name.back()=='/'||name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-/")!=std::string::npos)return false;
  for(const auto& part:Split(name,'/')) {
    if(part.empty()||part=="."||part==".."||part.back()=='.')return false;
    auto lower=Lower(part),stem=lower.substr(0,lower.find('.'));
    if(stem=="con"||stem=="prn"||stem=="aux"||stem=="nul"||stem=="clock$"||
        (stem.size()==4&&(stem.rfind("com",0)==0||stem.rfind("lpt",0)==0)&&stem.back()>='1'&&stem.back()<='9'))return false;
  }
  const auto folded=Lower(name);
  // Only the executable distribution is owned by the updater. Profiles,
  // updater journals, uninstaller and portable mode marker are never payloads.
  if(folded.rfind("resources/",0)==0||folded.rfind("locales/",0)==0||folded.rfind("docs/",0)==0) {
    if(Split(folded,'/').size()>8)return false;
    return folded.find("/profile/")==std::string::npos&&folded.find("/profiles/")==std::string::npos;
  }
  static const std::set<std::string> roots={"turanbrowser.exe","turanbrowser.dll","turanprofiletool.exe","turanupdatetool.exe",
      "libcef.dll","chrome_elf.dll","d3dcompiler_47.dll","dxil.dll","dxcompiler.dll","libegl.dll","libglesv2.dll","vulkan-1.dll","vk_swiftshader.dll",
      "vk_swiftshader_icd.json","chrome_100_percent.pak","chrome_200_percent.pak","resources.pak","icudtl.dat",
      "v8_context_snapshot.bin","cef-license.txt","cef-credits.html","license.txt","third_party_notices.md","readme.md",
      "test_report.md","release_notes.md","installation.md","security.md","privacy.md","performance_report.md","build-manifest.json"};
  return roots.count(folded)!=0;
}
bool HttpsUrlValid(const std::string& url) {
  if(url.size()>2048||url.rfind("https://",0)!=0||url.find_first_of("?#@\\\r\n\t ")!=std::string::npos)return false;
  auto slash=url.find('/',8);if(slash==std::string::npos||slash==8)return false;
  auto host=url.substr(8,slash-8);
  if(host.size()>253||host.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-")!=std::string::npos||
     host.front()=='.'||host.back()=='.'||host.find("..")!=std::string::npos||host.find('.')==std::string::npos)return false;
  for(char c:url)if(static_cast<unsigned char>(c)<33||static_cast<unsigned char>(c)>126)return false;
  for(const auto& p:Split(url.substr(slash+1),'/'))if(p=="."||p==".."||p.find('%')!=std::string::npos)return false;
  return true;
}
bool Configured(const Policy& p) {
  if(!PublicKey(p.rsa_public_blob)||!HttpsUrlValid(p.manifest_url)||
      (p.channel!="stable"&&p.channel!="development"&&p.channel!="beta")||p.runtime_paths.empty()||p.runtime_paths.size()>4096)return false;
  std::set<std::string> paths;for(const auto& name:p.runtime_paths)if(!RuntimePathValid(name)||!paths.insert(Lower(name)).second)return false;
  return paths.count("turanbrowser.exe")&&paths.count("turanbrowser.dll")&&paths.count("libcef.dll");
}
bool Authenticate(const Policy& p,const std::string& bytes,const std::string& signature,Manifest& m,std::string& error,bool recovery) {
  m={};error.clear();
  if(!Configured(p)){error="No authenticated NorthByte update channel has been provisioned.";return false;}
  if(bytes.empty()||bytes.size()>1024*1024||!Verify(p,bytes,signature)){error="The update manifest publisher signature is invalid.";return false;}
  if(!detail::ParseManifest(bytes,m,error))return false;
  std::set<std::string> inventory;for(const auto& path:p.runtime_paths)inventory.insert(path);
  for(const auto& file:m.files)if(!inventory.count(file.path)) {
    error="The signed update attempts to write outside the compiled runtime inventory.";return false;
  }
  if(m.files.size()!=inventory.size()) {
    error="The signed update omits files from the compiled runtime inventory. Use a full installer for inventory changes.";return false;
  }
  if(m.channel!=p.channel){error="The signed update belongs to another release channel.";return false;}
  if(!recovery&&(m.expires<detail::Now()||m.issued>detail::Now()+300)){error="The signed update metadata is expired or not yet valid.";return false;}
  return true;
}
namespace detail {
bool ParseManifest(const std::string& bytes,Manifest& m,std::string& error) {
  auto fail=[&](){error="The authenticated update manifest has an invalid schema or unsafe inventory.";return false;};
  if(bytes.empty()||bytes.back()!='\n'||bytes.find_first_of("\r\0",0,2)!=std::string::npos)return fail();
  for(unsigned char c:bytes)if((c<32&&c!='\n'&&c!='\t')||c>126)return fail();
  auto lines=Split(bytes,'\n');lines.pop_back();if(lines.size()<10||lines.size()>4105||lines[0]!="TURAN-UPDATE-1")return fail();
  auto field=[&](size_t i,const char* name,std::string& value){auto p=Split(lines[i],'\t');if(p.size()!=2||p[0]!=name)return false;value=p[1];return true;};
  std::string v,s,issued,expires,count;
  if(!field(1,"version",v)||!field(2,"sequence",s)||!field(3,"channel",m.channel)||!field(4,"kind",m.kind)||
     !field(5,"issued",issued)||!field(6,"expires",expires)||!field(7,"base",m.base_url)||!field(8,"files",count))return fail();
  auto parts=Split(v,'.');uint64_t number=0;uint32_t* values[]={&m.version.major,&m.version.minor,&m.version.patch,&m.version.revision};
  if(parts.size()!=4)return fail();for(size_t i=0;i<4;++i){if(!Number(parts[i],number,65535))return fail();*values[i]=static_cast<uint32_t>(number);}
  if(!Number(s,m.sequence)||!m.sequence||!Number(issued,m.issued)||!Number(expires,m.expires)||m.expires<=m.issued||
      m.expires-m.issued>31*86400||!Number(count,number,4096)||!number||lines.size()!=number+9||
      (m.channel!="stable"&&m.channel!="development"&&m.channel!="beta")||(m.kind!="portable"&&m.kind!="installer")||
      !HttpsUrlValid(m.base_url)||m.base_url.back()!='/')return fail();
  std::set<std::string> names;uint64_t total=0;std::string previous;
  for(size_t i=9;i<lines.size();++i) {
    auto p=Split(lines[i],'\t');File f;
    if(p.size()!=4||p[0]!="file"||!Number(p[1],f.bytes,2ULL*1024*1024*1024)||!Hex(p[2])||!RuntimePathValid(p[3]))return fail();
    f.sha256=p[2];f.path=p[3];auto folded=Lower(f.path);
    if(!names.insert(folded).second||(!previous.empty()&&f.path<=previous))return fail();
    for(const auto& n:names)if(n!=folded&&(n.rfind(folded+"/",0)==0||folded.rfind(n+"/",0)==0))return fail();
    previous=f.path;total+=f.bytes;if(total>4ULL*1024*1024*1024)return fail();m.files.push_back(std::move(f));
  }
  if(!names.count("turanbrowser.exe")||!names.count("turanbrowser.dll")||!names.count("libcef.dll"))return fail();
  return true;
}
bool Fresh(const Policy& policy,const Manifest& m,uint64_t previous,std::string& error) {
  if(!(policy.running_version<m.version)||m.sequence<=std::max(previous,policy.running_sequence)) {
    error="The signed update is a downgrade, replay or already installed release.";return false;
  }
  return true;
}
bool Receipt(const std::filesystem::path& app,const Policy& policy,Manifest& prior,bool& present,std::string& error) {
  present=false;auto meta=app/L".turan-update-receipt.manifest",sig=app/L".turan-update-receipt.sig";
  if(Missing(meta)&&Missing(sig))return true;
  std::string bytes,signature;
  if(!Read(meta,1024*1024,bytes)||!Read(sig,1024,signature)||!Authenticate(policy,bytes,signature,prior,error,true)) {
    error="The existing signed update receipt is invalid; files have been preserved.";return false;
  }
  present=true;return true;
}
}
}
