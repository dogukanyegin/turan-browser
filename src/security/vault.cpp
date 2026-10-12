#include "security/vault.h"
#include "core/model.h"
#include "core/state_file.h"
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <algorithm>
#include <cstring>
#include <set>
#include <limits>

namespace turan {
namespace {
constexpr size_t Limit=16*1024*1024, Records=2000;
constexpr uint64_t Iterations=600000;
constexpr char LocalMagic[]="TVLT1";
constexpr char ExportMagic[]="TVEX1";
constexpr char PayloadMagic[]="Turan credentials 1";
struct Alg {BCRYPT_ALG_HANDLE h=nullptr;~Alg(){if(h)BCryptCloseAlgorithmProvider(h,0);}};
struct Key {BCRYPT_KEY_HANDLE h=nullptr;~Key(){if(h)BCryptDestroyKey(h);}};
struct Transaction {
 HANDLE handle=INVALID_HANDLE_VALUE;
 explicit Transaction(const std::filesystem::path& file){
  auto parent=file.parent_path();auto drive=parent.root_name().wstring();
  if(!parent.is_absolute()||parent.lexically_normal()!=parent||drive.size()!=2||drive[1]!=L':'||file.native().size()>32700)return;
  for(auto cursor=parent;!cursor.empty();){auto attributes=GetFileAttributesW(cursor.c_str());if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY)||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return;auto next=cursor.parent_path();if(next==cursor)break;cursor=next;}
  // CREATE_NEW refuses existing/symlink lock leaves. Delete-on-close also
  // releases this owned transaction lease after an abrupt process death.
  handle=CreateFileW((file.wstring()+L".lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
 }
 ~Transaction(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
 bool valid()const{return handle!=INVALID_HANDLE_VALUE;}
};
bool Random(void* data,size_t size){return size<=ULONG_MAX&&BCryptGenRandom(nullptr,static_cast<PUCHAR>(data),static_cast<ULONG>(size),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0;}
Secret Derive(const Secret& pass,const std::array<unsigned char,16>& salt){
  Alg alg;Secret key(32);
  if(!key.valid()||BCryptOpenAlgorithmProvider(&alg.h,BCRYPT_SHA256_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG)<0||
    BCryptDeriveKeyPBKDF2(alg.h,const_cast<PUCHAR>(pass.data()),static_cast<ULONG>(pass.size()),const_cast<PUCHAR>(salt.data()),16,Iterations,key.data(),32,0)<0)return {};
  return key;
}
std::string Encrypt(const Secret& key,std::string_view plain,std::string_view aad){
  if(key.size()!=32||plain.size()>Limit)return {};
  Alg alg;DWORD object_size=0,count=0;
  if(BCryptOpenAlgorithmProvider(&alg.h,BCRYPT_AES_ALGORITHM,nullptr,0)<0||
     BCryptSetProperty(alg.h,BCRYPT_CHAINING_MODE,reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),sizeof(BCRYPT_CHAIN_MODE_GCM),0)<0||
     BCryptGetProperty(alg.h,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&object_size),sizeof(object_size),&count,0)<0)return {};
  Secret object(object_size);Key handle;if(!object.valid()||BCryptGenerateSymmetricKey(alg.h,&handle.h,object.data(),object_size,const_cast<PUCHAR>(key.data()),32,0)<0)return {};
  std::string out(12+16+plain.size(),'\0');if(!Random(out.data(),12))return {};
  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth;BCRYPT_INIT_AUTH_MODE_INFO(auth);
  auth.pbNonce=reinterpret_cast<PUCHAR>(out.data());auth.cbNonce=12;
  auth.pbTag=reinterpret_cast<PUCHAR>(out.data()+12);auth.cbTag=16;
  auth.pbAuthData=reinterpret_cast<PUCHAR>(const_cast<char*>(aad.data()));auth.cbAuthData=static_cast<ULONG>(aad.size());
  ULONG written=0;
  if(BCryptEncrypt(handle.h,reinterpret_cast<PUCHAR>(const_cast<char*>(plain.data())),static_cast<ULONG>(plain.size()),&auth,nullptr,0,
      reinterpret_cast<PUCHAR>(out.data()+28),static_cast<ULONG>(plain.size()),&written,0)<0||written!=plain.size())return {};
  return out;
}
Secret Decrypt(const Secret& key,std::string_view cipher,std::string_view aad){
  if(key.size()!=32||cipher.size()<28||cipher.size()>Limit)return {};
  Alg alg;DWORD object_size=0,count=0;
  if(BCryptOpenAlgorithmProvider(&alg.h,BCRYPT_AES_ALGORITHM,nullptr,0)<0||
     BCryptSetProperty(alg.h,BCRYPT_CHAINING_MODE,reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),sizeof(BCRYPT_CHAIN_MODE_GCM),0)<0||
     BCryptGetProperty(alg.h,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&object_size),sizeof(object_size),&count,0)<0)return {};
  Secret object(object_size);Key handle;if(!object.valid()||BCryptGenerateSymmetricKey(alg.h,&handle.h,object.data(),object_size,const_cast<PUCHAR>(key.data()),32,0)<0)return {};
  Secret plain(cipher.size()-28);if(!plain.valid())return {};
  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth;BCRYPT_INIT_AUTH_MODE_INFO(auth);
  auth.pbNonce=reinterpret_cast<PUCHAR>(const_cast<char*>(cipher.data()));auth.cbNonce=12;
  auth.pbTag=reinterpret_cast<PUCHAR>(const_cast<char*>(cipher.data()+12));auth.cbTag=16;
  auth.pbAuthData=reinterpret_cast<PUCHAR>(const_cast<char*>(aad.data()));auth.cbAuthData=static_cast<ULONG>(aad.size());
  ULONG written=0;
  if(BCryptDecrypt(handle.h,reinterpret_cast<PUCHAR>(const_cast<char*>(cipher.data()+28)),static_cast<ULONG>(cipher.size()-28),&auth,nullptr,0,plain.data(),static_cast<ULONG>(plain.size()),&written,0)<0||written!=plain.size())return {};
  return plain;
}
std::string Protect(std::string_view plain){
  DATA_BLOB input={static_cast<DWORD>(plain.size()),reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))},out={};
  if(!CryptProtectData(&input,L"Turan password vault",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out))return {};
  std::string bytes(reinterpret_cast<char*>(out.pbData),out.cbData);SecureZeroMemory(out.pbData,out.cbData);LocalFree(out.pbData);return bytes;
}
Secret Unprotect(std::string_view bytes){
  DATA_BLOB input={static_cast<DWORD>(bytes.size()),reinterpret_cast<BYTE*>(const_cast<char*>(bytes.data()))},out={};
  if(!CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out))return {};
  Secret plain(out.cbData);if(plain.valid())memcpy(plain.data(),out.pbData,out.cbData);SecureZeroMemory(out.pbData,out.cbData);LocalFree(out.pbData);return plain;
}
void Number(std::string& out,uint64_t value){for(unsigned n=0;n<8;++n)out.push_back(static_cast<char>(value>>(n*8)));}
void Field(std::string& out,std::string_view value){Number(out,value.size());out.append(value);}
struct Reader {std::string_view bytes;size_t pos=0;bool ok=true;
 uint64_t Number(){if(bytes.size()-pos<8){ok=false;return 0;}uint64_t n=0;for(unsigned i=0;i<8;++i)n|=uint64_t(static_cast<unsigned char>(bytes[pos++]))<<(i*8);return n;}
 std::string Field(size_t maximum){auto length=Number();if(!ok||length>maximum||length>bytes.size()-pos){ok=false;return {};}auto out=std::string(bytes.substr(pos,static_cast<size_t>(length)));pos+=static_cast<size_t>(length);return out;}
};
bool Text(std::string_view text,size_t maximum,bool empty=false){if(text.size()>maximum||(!empty&&text.empty()))return false;for(unsigned char ch:text)if(ch<32||ch==127)return false;return text.empty()||MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0)>0;}
std::string Id(){unsigned char bytes[16];if(!Random(bytes,sizeof(bytes)))return {};constexpr char hex[]="0123456789abcdef";std::string id;for(auto ch:bytes){id+=hex[ch>>4];id+=hex[ch&15];}return id;}
std::string Aad(const CredentialInfo& info){std::string out;Field(out,info.id);Field(out,info.origin);Field(out,info.username);Number(out,info.updated);return out;}
uint64_t Now(){FILETIME time;GetSystemTimeAsFileTime(&time);return (uint64_t(time.dwHighDateTime)<<32)|time.dwLowDateTime;}
std::string Header(const char* magic,const std::array<unsigned char,16>& salt){std::string out(magic,5);out.append(reinterpret_cast<const char*>(salt.data()),salt.size());Number(out,Iterations);return out;}
bool WriteNew(const std::filesystem::path& path,const std::string& bytes){HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(h==INVALID_HANDLE_VALUE)return false;DWORD count=0;bool ok=bytes.size()<=MAXDWORD&&WriteFile(h,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size()&&FlushFileBuffers(h);CloseHandle(h);if(!ok)DeleteFileW(path.c_str());return ok;}
}
PasswordVault::PasswordVault(std::filesystem::path profile):root_(std::move(profile)),file_(root_/L"password-vault.dat"){}
bool PasswordVault::Exists() const {std::string bytes;return ReadStateFile(file_,bytes,Limit)!=StateRead::Missing;}
bool PasswordVault::OriginValid(const std::string& value){
 if(!Text(value,2048)||value.rfind("https://",0)!=0||Origin(value)!=value||value.find_first_of("@%\\ \"<>")!=std::string::npos)return false;
 auto wide=Wide(value);URL_COMPONENTS parts={};parts.dwStructSize=sizeof(parts);parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=parts.dwUserNameLength=parts.dwPasswordLength=static_cast<DWORD>(-1);
 return WinHttpCrackUrl(wide.c_str(),static_cast<DWORD>(wide.size()),0,&parts)&&parts.nScheme==INTERNET_SCHEME_HTTPS&&parts.dwHostNameLength>0&&parts.nPort>0&&!parts.dwUrlPathLength&&!parts.dwExtraInfoLength&&!parts.dwUserNameLength&&!parts.dwPasswordLength;
}
bool PasswordVault::PassphraseValid(const Secret& value){if(!SecretUtf8Valid(value,1024))return false;size_t scalars=0;for(unsigned char ch:value.view())if((ch&0xc0)!=0x80)++scalars;return scalars>=15;}
bool PasswordVault::Unlocked() const {std::lock_guard<std::mutex> guard(mutex_);return key_.size()==32;}
void PasswordVault::Lock(){std::lock_guard<std::mutex> guard(mutex_);key_.Clear();records_.clear();disk_.clear();salt_.fill(0);}
std::vector<CredentialInfo> PasswordVault::List(const std::string& search) const {std::lock_guard<std::mutex> guard(mutex_);std::vector<CredentialInfo> out;if(key_.size()!=32)return out;for(const auto& row:records_)if(search.empty()||row.info.origin.find(search)!=std::string::npos||row.info.username.find(search)!=std::string::npos)out.push_back(row.info);return out;}
// The authenticated payload contains only metadata and already encrypted password records.
// Metadata is also encrypted on disk; plaintext passwords live only in Secret allocations.
bool PasswordVault::Publish(const std::vector<Record>& next,std::string& error){
 if(key_.size()!=32||next.size()>Records){error="Vault is locked or record limit exceeded.";return false;}
 Transaction transaction(file_);if(!transaction.valid()){error="Another vault transaction is active, or the profile path is unsafe; no data changed.";return false;}
 std::string payload=PayloadMagic;Number(payload,next.size());
 for(const auto& row:next){Field(payload,row.info.id);Field(payload,row.info.origin);Field(payload,row.info.username);Number(payload,row.info.updated);Field(payload,row.cipher);}
 auto header=Header(LocalMagic,salt_);auto encrypted=Encrypt(key_,payload,header);SecureZeroMemory(payload.data(),payload.size());
 if(encrypted.empty()){error="Vault encryption failed.";return false;}
 auto protected_bytes=Protect(header+encrypted);if(protected_bytes.empty()){error="Windows DPAPI protection failed.";return false;}
 std::string next_disk=std::string(LocalMagic,5)+protected_bytes,current;auto read=ReadStateFile(file_,current,Limit);
 if((disk_.empty()&&read!=StateRead::Missing)||(!disk_.empty()&&(read!=StateRead::Read||current!=disk_))){error="Vault file changed; reload before saving.";return false;}
 if(!disk_.empty()&&!WriteStateFile(file_.wstring()+L".bak",disk_)){error="Vault backup failed; original preserved.";return false;}
 if(!WriteStateFile(file_,next_disk)){error="Vault write failed; original preserved.";return false;}
 disk_=std::move(next_disk);records_=next;error.clear();return true;
}
bool PasswordVault::Create(const Secret& passphrase,std::string& error){std::lock_guard<std::mutex> guard(mutex_);if(!PassphraseValid(passphrase)){error="Use a master passphrase of at least 15 Unicode characters.";return false;}
 if(Exists()){error="A vault already exists; unlock it instead.";return false;}if(!Random(salt_.data(),salt_.size())){error="Random generation failed.";return false;}
 key_=Derive(passphrase,salt_);disk_.clear();records_.clear();if(!Publish({},error)){key_.Clear();return false;}return true;
}
bool PasswordVault::Open(const std::string& disk,const Secret& passphrase,std::vector<Record>& records,Secret& key,std::array<unsigned char,16>& salt,std::string& error) const {
 error="Incorrect passphrase, damaged vault, or a different Windows account.";
 if(!SecretUtf8Valid(passphrase,1024)||disk.size()<5||disk.size()>Limit)return false;
 const bool local=disk.compare(0,5,LocalMagic)==0,portable=disk.compare(0,5,ExportMagic)==0;if(!local&&!portable)return false;
 Secret protected_plain;std::string_view bytes=disk;
 if(local){protected_plain=Unprotect(std::string_view(disk).substr(5));if(!protected_plain.valid()||protected_plain.empty())return false;bytes=protected_plain.view();}
 if(bytes.size()<29+28||bytes.substr(0,5)!=(local?LocalMagic:ExportMagic))return false;
 memcpy(salt.data(),bytes.data()+5,16);Reader header{bytes.substr(21,8)};if(header.Number()!=Iterations)return false;
 key=Derive(passphrase,salt);if(key.empty())return false;
 auto payload=Decrypt(key,bytes.substr(29),bytes.substr(0,29));if(payload.empty()||payload.view().substr(0,sizeof(PayloadMagic)-1)!=PayloadMagic)return false;
 Reader reader{payload.view(),sizeof(PayloadMagic)-1};auto count=reader.Number();if(count>Records)return false;
 std::set<std::string> ids,pairs;std::vector<Record> next;
 for(uint64_t i=0;i<count&&reader.ok;++i){Record row;row.info.id=reader.Field(32);row.info.origin=reader.Field(2048);row.info.username=reader.Field(1024);row.info.updated=reader.Number();row.cipher=reader.Field(28+4096);
  if(!reader.ok||row.info.id.size()!=32||row.info.id.find_first_not_of("0123456789abcdef")!=std::string::npos||!OriginValid(row.info.origin)||!Text(row.info.username,1024,true)||!ids.insert(row.info.id).second||!pairs.insert(row.info.origin+'\0'+row.info.username).second)return false;
  auto password=Decrypt(key,row.cipher,Aad(row.info));if(!SecretUtf8Valid(password,4096))return false;next.push_back(std::move(row));
 }
 if(!reader.ok||reader.pos!=reader.bytes.size())return false;records=std::move(next);error.clear();return true;
}
bool PasswordVault::Unlock(const Secret& passphrase,std::string& error){std::lock_guard<std::mutex> guard(mutex_);std::string bytes;if(ReadStateFile(file_,bytes,Limit)!=StateRead::Read){error="Vault cannot be read; no data changed.";return false;}
 std::vector<Record> next;Secret key;std::array<unsigned char,16> salt;if(!Open(bytes,passphrase,next,key,salt,error))return false;records_=std::move(next);key_=std::move(key);salt_=salt;disk_=std::move(bytes);return true;
}
bool PasswordVault::Reauthenticate(const Secret& passphrase,std::string& error) const {std::lock_guard<std::mutex> guard(mutex_);if(key_.size()!=32){error="Vault is locked.";return false;}std::string current;if(ReadStateFile(file_,current,Limit)!=StateRead::Read||current!=disk_){error="Vault file changed; unlock again before continuing.";return false;}std::vector<Record> rows;Secret key;std::array<unsigned char,16> salt;return Open(current,passphrase,rows,key,salt,error);}
bool PasswordVault::Save(const std::string& origin,const std::string& username,const Secret& password,std::string& error){std::lock_guard<std::mutex> guard(mutex_);if(!OriginValid(origin)||!Text(username,1024,true)||!SecretUtf8Valid(password,4096)){error="Credentials require an exact HTTPS origin and valid bounded text.";return false;}if(key_.size()!=32){error="Vault is locked.";return false;}
 auto next=records_;auto at=std::find_if(next.begin(),next.end(),[&](const Record& r){return r.info.origin==origin&&r.info.username==username;});if(at==next.end()){if(next.size()>=Records){error="Vault record limit reached.";return false;}Record row;row.info={Id(),origin,username,Now()};if(row.info.id.empty()){error="Random generation failed.";return false;}next.push_back(std::move(row));at=next.end()-1;}else at->info.updated=Now();
 at->cipher=Encrypt(key_,password.view(),Aad(at->info));if(at->cipher.empty()){error="Credential encryption failed.";return false;}return Publish(next,error);
}
bool PasswordVault::Remove(const std::string& id,std::string& error){std::lock_guard<std::mutex> guard(mutex_);auto next=records_;auto at=std::find_if(next.begin(),next.end(),[&](const Record& r){return r.info.id==id;});if(at==next.end()){error="Credential not found.";return false;}next.erase(at);return Publish(next,error);}
Secret PasswordVault::Reveal(const std::string& id,const std::string& exact_origin,std::string& error) const {std::lock_guard<std::mutex> guard(mutex_);if(key_.size()!=32){error="Vault is locked.";return {};}
 auto at=std::find_if(records_.begin(),records_.end(),[&](const Record& r){return r.info.id==id&&r.info.origin==exact_origin;});if(at==records_.end()){error="Credential or exact origin does not match.";return {};}
 auto out=Decrypt(key_,at->cipher,Aad(at->info));if(!SecretUtf8Valid(out,4096)){error="Credential decryption failed.";return {};}error.clear();return out;
}
bool PasswordVault::ChangePassphrase(const Secret& passphrase,std::string& error){std::lock_guard<std::mutex> guard(mutex_);if(key_.size()!=32||!PassphraseValid(passphrase)){error="Unlock the vault and use a passphrase of at least 15 Unicode characters.";return false;}
 auto next=records_;std::array<unsigned char,16> salt;if(!Random(salt.data(),salt.size())){error="Random generation failed.";return false;}auto key=Derive(passphrase,salt);if(key.empty()){error="Key derivation failed.";return false;}
 for(auto& row:next){auto plain=Decrypt(key_,row.cipher,Aad(row.info));if(!SecretUtf8Valid(plain,4096)){error="Credential decryption failed.";return false;}row.cipher=Encrypt(key,plain.view(),Aad(row.info));if(row.cipher.empty()){error="Credential encryption failed.";return false;}}
 Secret old=std::move(key_);auto old_salt=salt_;key_=std::move(key);salt_=salt;if(Publish(next,error))return true;key_=std::move(old);salt_=old_salt;return false;
}
bool PasswordVault::Export(const std::filesystem::path& output,const Secret& passphrase,std::string& error) const {std::lock_guard<std::mutex> guard(mutex_);if(key_.size()!=32||!PassphraseValid(passphrase)){error="Unlock the vault and use an export passphrase of at least 15 Unicode characters.";return false;}
 std::array<unsigned char,16> salt;if(!Random(salt.data(),salt.size())){error="Random generation failed.";return false;}auto key=Derive(passphrase,salt);if(key.empty()){error="Key derivation failed.";return false;}
 std::string payload=PayloadMagic;Number(payload,records_.size());for(const auto& row:records_){auto password=Decrypt(key_,row.cipher,Aad(row.info));if(!SecretUtf8Valid(password,4096)){error="Credential decryption failed.";return false;}auto cipher=Encrypt(key,password.view(),Aad(row.info));if(cipher.empty()){error="Export encryption failed.";return false;}Field(payload,row.info.id);Field(payload,row.info.origin);Field(payload,row.info.username);Number(payload,row.info.updated);Field(payload,cipher);}
 auto header=Header(ExportMagic,salt);auto encrypted=Encrypt(key,payload,header);SecureZeroMemory(payload.data(),payload.size());if(encrypted.empty()||!WriteNew(output,header+encrypted)){error="Export failed; choose a new file in an existing user folder.";return false;}error.clear();return true;
}
bool PasswordVault::Import(const std::filesystem::path& input,const Secret& passphrase,std::string& error,size_t& imported){std::lock_guard<std::mutex> guard(mutex_);imported=0;if(key_.size()!=32){error="Vault is locked.";return false;}
 std::string disk;if(ReadStateFile(input,disk,Limit)!=StateRead::Read||disk.compare(0,5,ExportMagic)!=0){error="Select a valid encrypted Turan export.";return false;}std::vector<Record> rows;Secret key;std::array<unsigned char,16> salt;if(!Open(disk,passphrase,rows,key,salt,error))return false;
 auto next=records_;for(auto& row:rows){auto plain=Decrypt(key,row.cipher,Aad(row.info));if(!SecretUtf8Valid(plain,4096)){error="Import decryption failed.";return false;}
  auto existing=std::find_if(next.begin(),next.end(),[&](const Record& r){return r.info.origin==row.info.origin&&r.info.username==row.info.username;});
  if(existing!=next.end())row.info.id=existing->info.id;else row.info.id=Id();if(row.info.id.empty()){error="Random generation failed.";return false;}row.info.updated=Now();row.cipher=Encrypt(key_,plain.view(),Aad(row.info));if(row.cipher.empty()){error="Import encryption failed.";return false;}
  if(existing==next.end()){if(next.size()>=Records){error="Import exceeds record limit; no changes made.";return false;}next.push_back(std::move(row));}else *existing=std::move(row);
 }
 if(!Publish(next,error))return false;imported=rows.size();return true;
}
Secret PasswordVault::Generate(size_t length){if(length<16||length>128)return {};constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()-_=+";constexpr unsigned limit=256-(256%(sizeof(alphabet)-1));Secret out(length);if(!out.valid())return {};size_t at=0;while(at<length){unsigned char byte;if(!Random(&byte,1))return {};if(byte<limit)out.data()[at++]=alphabet[byte%(sizeof(alphabet)-1)];}return out;}
}
