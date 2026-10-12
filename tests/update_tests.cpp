#include "update/updater.h"
#include "update/internal.h"
#include "ui/update_window.h"
#include <bcrypt.h>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>

using namespace turan::update;
namespace fs=std::filesystem;
namespace {
void Need(bool test,const std::string& message){if(!test)throw std::runtime_error(message);}
struct Key {
  BCRYPT_ALG_HANDLE provider=nullptr;BCRYPT_KEY_HANDLE key=nullptr;std::vector<unsigned char> pub;
  Key() {
    Need(BCryptOpenAlgorithmProvider(&provider,BCRYPT_RSA_ALGORITHM,nullptr,0)>=0,"RSA provider");
    Need(BCryptGenerateKeyPair(provider,&key,3072,0)>=0&&BCryptFinalizeKeyPair(key,0)>=0,"Generate disposable test signing key");
    ULONG size=0;Need(BCryptExportKey(key,nullptr,BCRYPT_RSAPUBLIC_BLOB,nullptr,0,&size,0)>=0,"Export public key size");pub.resize(size);
    Need(BCryptExportKey(key,nullptr,BCRYPT_RSAPUBLIC_BLOB,pub.data(),size,&size,0)>=0,"Export test public key");
  }
  ~Key(){if(key)BCryptDestroyKey(key);if(provider)BCryptCloseAlgorithmProvider(provider,0);}
  std::string Sign(const std::string& bytes) {
    auto hex=Sha256(bytes);unsigned char hash[32];
    for(size_t i=0;i<32;++i){auto n=[](char c){return c<='9'?c-'0':c-'a'+10;};hash[i]=static_cast<unsigned char>(n(hex[2*i])*16+n(hex[2*i+1]));}
    BCRYPT_PSS_PADDING_INFO padding={BCRYPT_SHA256_ALGORITHM,32};ULONG size=0;
    Need(BCryptSignHash(key,&padding,hash,32,nullptr,0,&size,BCRYPT_PAD_PSS)>=0,"PSS signature size");std::string sig(size,'\0');
    Need(BCryptSignHash(key,&padding,hash,32,reinterpret_cast<PUCHAR>(sig.data()),size,&size,BCRYPT_PAD_PSS)>=0,"PSS signature");return sig;
  }
};
struct Fixture {
  fs::path directory,app;
  std::map<std::string,std::string> payload={{"TuranBrowser.dll","new browser DLL"},{"TuranBrowser.exe","new bootstrap"},
      {"libcef.dll","new CEF DLL"},{"resources/test.txt","new resource"}};
  explicit Fixture(fs::path path):directory(path),app(path/L"app") {
    fs::create_directories(app/L"resources");fs::create_directories(app/L"Profile"/L"Cache");fs::create_directories(app/L"Profiles"/L"private-test");
    Need(detail::Write(app/L"TuranBrowser.exe","old bootstrap"),"old exe");
    Need(detail::Write(app/L"TuranBrowser.dll","old browser DLL"),"old DLL");
    Need(detail::Write(app/L"libcef.dll","old CEF DLL"),"old CEF");
    Need(detail::Write(app/L"resources"/L"test.txt","old resource"),"old resource");
    Need(detail::Write(app/L"portable.flag","Portable profile: Profile/\n"),"portable marker");
    Need(detail::Write(app/L"Profile"/L"browser-state.txt","S fictional settings\nB fictional bookmark\n"),"profile state");
    Need(detail::Write(app/L"Profile"/L"Cache"/L"Cookies","fictional opaque profile cookie bytes"),"cookie fixture");
    Need(detail::Write(app/L"Profiles"/L"private-test"/L"vault.bin","fictional encrypted vault bytes"),"vault fixture");
    Need(detail::Write(app/L"Uninstall.exe","existing uninstaller"),"uninstaller fixture");
  }
  std::string Metadata(std::string kind="portable",Version v={0,3,0,0},uint64_t sequence=2,uint64_t issued=0,uint64_t expires=0) const {
    if(!issued)issued=detail::Now();if(!expires)expires=issued+86400;
    std::ostringstream out;out<<"TURAN-UPDATE-1\nversion\t"<<v.major<<'.'<<v.minor<<'.'<<v.patch<<'.'<<v.revision<<
        "\nsequence\t"<<sequence<<"\nchannel\tdevelopment\nkind\t"<<kind<<"\nissued\t"<<issued<<"\nexpires\t"<<expires<<
        "\nbase\thttps://updates.example.invalid/releases/0.3.0/\nfiles\t"<<payload.size()<<'\n';
    for(const auto& p:payload)out<<"file\t"<<p.second.size()<<'\t'<<Sha256(p.second)<<'\t'<<p.first<<'\n';return out.str();
  }
  Offer OfferFor(Key& key,std::string kind="portable",Version v={0,3,0,0},uint64_t sequence=2) const {
    Offer offer;offer.manifest_bytes=Metadata(kind,v,sequence);offer.signature=key.Sign(offer.manifest_bytes);return offer;
  }
  Fetch Fetcher() const {
    return [this](const std::string& url,const fs::path& path,uint64_t bound,std::string& error) {
      std::string base="https://updates.example.invalid/releases/0.3.0/";
      if(url.rfind(base,0)!=0){error="Unknown fixture origin";return false;}
      auto entry=payload.find(url.substr(base.size()));
      if(entry==payload.end()||entry->second.size()!=bound){error="Unknown fixture payload";return false;}
      return detail::Write(path,entry->second);
    };
  }
  std::string Read(const fs::path& relative) const {std::string out;Need(detail::Read(app/relative,65536,out),"Read fixture "+relative.string());return out;}
  void Original() const {
    Need(Read(L"TuranBrowser.exe")=="old bootstrap"&&Read(L"TuranBrowser.dll")=="old browser DLL"&&Read(L"libcef.dll")=="old CEF DLL"&&Read(L"resources/test.txt")=="old resource","Original runtime preserved/restored");
    Profiles();
  }
  void Profiles() const {
    Need(Read(L"Profile/browser-state.txt")=="S fictional settings\nB fictional bookmark\n"&&Read(L"Profile/Cache/Cookies")=="fictional opaque profile cookie bytes"&&
        Read(L"Profiles/private-test/vault.bin")=="fictional encrypted vault bytes"&&Read(L"portable.flag")=="Portable profile: Profile/\n"&&
        Read(L"Uninstall.exe")=="existing uninstaller","All profile/portable/uninstaller fixture bytes preserved");
  }
};
Policy TestPolicy(const std::vector<unsigned char>& pub) {Policy p;p.rsa_public_blob=pub;p.manifest_url="https://updates.example.invalid/development/portable.manifest";p.channel="development";p.runtime_paths={"TuranBrowser.dll","TuranBrowser.exe","libcef.dll","resources/test.txt"};p.running_version={0,2,0,0};p.running_sequence=1;return p;}
fs::path StageFixture(Fixture& f,const Policy& p,Key& key) {fs::path stage;std::string error;Need(Stage(f.app,p,f.OfferFor(key),stage,error,f.Fetcher()),"Stage: "+error);return stage;}
std::wstring Quote(const fs::path& p){return L"\""+p.wstring()+L"\"";}
void CrashChild(const fs::path& app,const fs::path& stage,const fs::path& public_key,size_t point) {
  wchar_t executable[32768];Need(GetModuleFileNameW(nullptr,executable,32768)!=0,"Test executable path");
  std::wstring command=L"\""+std::wstring(executable)+L"\" --crash "+Quote(app)+L" "+Quote(stage)+L" "+Quote(public_key)+L" "+std::to_wstring(point);
  STARTUPINFOW info={sizeof info};PROCESS_INFORMATION process={};
  Need(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&info,&process)!=FALSE,"Create native crash-recovery child");
  CloseHandle(process.hThread);Need(WaitForSingleObject(process.hProcess,30000)==WAIT_OBJECT_0,"Crash-recovery child wait");DWORD code=0;
  Need(GetExitCodeProcess(process.hProcess,&code)&&code==77,"Injected interruption occurred");CloseHandle(process.hProcess);
}
}
int wmain(int argc,wchar_t** argv) {
  try {
    if(argc==6&&std::wstring(argv[1])==L"--crash") {
      std::string blob;Need(detail::Read(argv[4],4096,blob),"Child test public key");auto p=TestPolicy(std::vector<unsigned char>(blob.begin(),blob.end()));std::string error;
      size_t point=static_cast<size_t>(_wtoi(argv[5]));
      ApplyForTest(argv[2],argv[3],p,error,[point](size_t step){if(step==point)TerminateProcess(GetCurrentProcess(),77);return true;});
      std::cerr<<error;return 98;
    }
    if(argc==4&&std::wstring(argv[1])==L"--download") {
      std::wstring wide=argv[2];std::string url;for(wchar_t c:wide){Need(c<128,"ASCII test URL");url+=static_cast<char>(c);}std::string bytes,error;
      bool ok=DownloadBytes(url,128,bytes,error);Need(!ok&&bytes.empty(),"Unsafe/TLS response rejected without retaining bytes");
      std::cout<<"PASS rejected HTTPS/HTTP response: "<<error<<'\n';return 0;
    }
    if(argc==5&&std::wstring(argv[1])==L"--authenticate") {
      std::string blob,metadata,signature;Need(detail::Read(argv[2],4096,blob)&&detail::Read(argv[3],1024*1024,metadata)&&detail::Read(argv[4],1024,signature),"External signer fixture read");
      auto p=TestPolicy(std::vector<unsigned char>(blob.begin(),blob.end()));Manifest m;std::string error;
      Need(Authenticate(p,metadata,signature,m,error),"OpenSSL publisher signer/CNG verifier interoperability: "+error);
      std::cout<<"PASS OpenSSL RSA-PSS signer / native CNG verifier\n";return 0;
    }
    Need(argc==2,"Use an explicit owned artifacts directory");fs::path artifact=fs::absolute(argv[1]);
    auto root=artifact/("update-native-tests-"+detail::RandomId());fs::create_directories(root);
    Key key;auto policy=TestPolicy(key.pub);std::string error;Manifest manifest;size_t checks=0;
    auto pass=[&](const char* name){++checks;std::cout<<"PASS "<<name<<'\n';};
    Need(!Configured(PublisherPolicy()),"Default build has no test or fabricated publisher trust root");pass("unconfigured publisher fails closed");
    HWND panel=turan::ShowUpdateWindow(nullptr,root,false,{});Need(panel!=nullptr,"Create real native updater panel");
    wchar_t status[1024]={};GetWindowTextW(GetDlgItem(panel,305),status,1024);
    Need(!IsWindowEnabled(GetDlgItem(panel,301))&&!IsWindowEnabled(GetDlgItem(panel,302))&&!IsWindowEnabled(GetDlgItem(panel,303))&&
        std::wstring(status).find(L"not been provisioned")!=std::wstring::npos,"Unconfigured native panel has accurate status and no fake actionable button");
    // Force the native test command to create a worker, close its real panel,
    // then join before the updater code can unload. No network is configured.
    SendMessageW(panel,WM_COMMAND,MAKEWPARAM(301,BN_CLICKED),0);DestroyWindow(panel);turan::WaitForUpdateWorkers();
    pass("native unconfigured update UI safety and shutdown join");
    Need(Sha256("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","CNG SHA256 known vector");pass("SHA256 known vector");
    Fixture canonical(root/L"canonical");auto offer=canonical.OfferFor(key);
    Need(Authenticate(policy,offer.manifest_bytes,offer.signature,manifest,error),"Real CNG RSA-PSS signed metadata: "+error);pass("real RSA3072 PSS SHA256 verification");
    auto changed=offer.manifest_bytes;changed[changed.find("0.3.0.0")]='1';
    Need(!Authenticate(policy,changed,offer.signature,manifest,error),"Manifest mutation rejected");
    auto badsig=offer.signature;badsig[0]^=1;Need(!Authenticate(policy,offer.manifest_bytes,badsig,manifest,error),"Signature mutation rejected");
    Key impostor;Need(!Authenticate(TestPolicy(impostor.pub),offer.manifest_bytes,offer.signature,manifest,error),"Other publisher rejected");pass("manifest/signature/key tamper rejected");
    for(const auto& path:{"../Profile/Cookies","Profile/Cookies","Profiles/default/vault.bin","Uninstall.exe","portable.flag","resources/../Profile","resources/CON.txt","resources/a.","resources/a:stream","resources/a\\b","resources//a","/resources/a"})Need(!RuntimePathValid(path),std::string("Unsafe inventory rejected: ")+path);
    for(const auto& path:{"TuranBrowser.exe","resources/clean.js","locales/tr.pak","docs/SECURITY.md"})Need(RuntimePathValid(path),std::string("Owned runtime path accepted: ")+path);
    for(const auto& url:{"http://updates.example.invalid/a","https://user@updates.example.invalid/a","https://updates.example.invalid/a?token=x","https://updates.example.invalid/../a","https://updates.example.invalid/%2e%2e/a","https://updates.example.invalid:443/a","https://updates.example.invalid/a\r\nInjected: 1"})Need(!HttpsUrlValid(url),"Unsafe URL rejected");pass("profile/path/URL/device/ADS boundaries");
    auto schema=offer.manifest_bytes;auto name=schema.find("resources/test.txt");schema.replace(name,18,"resources/CON.txt");
    Need(!Authenticate(policy,schema,key.Sign(schema),manifest,error),"Authenticated unsafe schema rejected");
    auto outside=offer.manifest_bytes;outside.replace(outside.find("resources/test.txt"),18,"resources/tor-profile-vault.bin");
    Need(!Authenticate(policy,outside,key.Sign(outside),manifest,error),"Even a signed publisher cannot write outside the immutable runtime inventory");
    auto expired=canonical.Metadata("portable",{0,3,0,0},2,detail::Now()-172800,detail::Now()-86400);
    Need(!Authenticate(policy,expired,key.Sign(expired),manifest,error),"Expired signed metadata rejected");
    auto future=canonical.Metadata("portable",{0,3,0,0},2,detail::Now()+1000,detail::Now()+2000);
    Need(!Authenticate(policy,future,key.Sign(future),manifest,error),"Future signed metadata rejected");
    auto other_channel=policy;other_channel.channel="stable";
    Need(!Authenticate(other_channel,offer.manifest_bytes,offer.signature,manifest,error),"Channel isolation");pass("signed schema/freshness/channel guards");
    fs::path stage;
    Need(!Stage(canonical.app,policy,offer,stage,error,canonical.Fetcher(),[]{return true;}),"Cancelled preparation stops before staging");canonical.Original();
    std::string cancelled_bytes;
    Need(!DownloadBytes("https://updates.example.invalid/a",1024,cancelled_bytes,error,[]{return true;})&&cancelled_bytes.empty(),"Cancelled network operation performs no request");
    Need(!Stage(canonical.app,policy,canonical.OfferFor(key,"installer"),stage,error,canonical.Fetcher()),"Installer/portable separation");canonical.Original();
    Need(!Stage(canonical.app,policy,canonical.OfferFor(key,"portable",{0,1,0,0},3),stage,error,canonical.Fetcher()),"Version downgrade");
    Need(!Stage(canonical.app,policy,canonical.OfferFor(key,"portable",{0,3,0,0},1),stage,error,canonical.Fetcher()),"Sequence replay");pass("downgrade/replay/package-kind prevention");
    Fixture corrupt(root/L"corrupt-stage");stage=StageFixture(corrupt,policy,key);
    Need(detail::Write(stage/L"payload"/L"TuranBrowser.dll","tampered",true),"Mutate staged DLL");
    Need(!Apply(corrupt.app,stage,policy,error),"Staged payload mutation rejected");corrupt.Original();pass("payload SHA256 tamper preserves runtime/profiles");
    Fixture junk(root/L"extra-stage");stage=StageFixture(junk,policy,key);Need(detail::Write(stage/L"payload"/L"resources"/L"unlisted.dll","unlisted"),"Unlisted staged DLL");
    Need(!Apply(junk.app,stage,policy,error),"Unlisted staged file rejected");junk.Original();pass("unlisted payload rejected");
    Fixture hardlink(root/L"hardlink-stage");stage=StageFixture(hardlink,policy,key);
    Need(CreateHardLinkW((stage/L"also-dll").c_str(),(stage/L"payload"/L"TuranBrowser.dll").c_str(),nullptr)!=FALSE,"Create owned hardlink test");
    Need(!Apply(hardlink.app,stage,policy,error),"Hardlinked payload rejected");hardlink.Original();pass("hardlink payload rejected");
    Fixture rollback(root/L"replacement-failure");stage=StageFixture(rollback,policy,key);
    Need(!ApplyForTest(rollback.app,stage,policy,error,[](size_t step){return step<3;}),"Injected failure rolls back");
    rollback.Original();Need(detail::Missing(rollback.app/L".turan-update-transaction"),"Failed transaction cleared after rollback");pass("replacement failure restores previous bytes");
    auto pubfile=root/L"disposable-public.blob";
    Need(detail::Write(pubfile,std::string(reinterpret_cast<const char*>(key.pub.data()),key.pub.size())),"Save test public key for child");
    for(size_t point:{1,2,3,7,9,10}) {
      Fixture interrupted(root/(L"interruption-"+std::to_wstring(point)));stage=StageFixture(interrupted,policy,key);
      CrashChild(interrupted.app,stage,pubfile,point);
      Need(Recover(interrupted.app,policy,error),"Recover terminated transaction: "+error);interrupted.Original();
      Need(Recover(interrupted.app,policy,error),"Recovery is idempotent");
    }
    pass("six real process terminations recover including missing browser DLL");
    Fixture ambiguous(root/L"tampered-recovery");stage=StageFixture(ambiguous,policy,key);CrashChild(ambiguous.app,stage,pubfile,2);
    Need(detail::Write(ambiguous.app/L"TuranBrowser.dll","modified after interrupted install",true),"Mutate partially installed target");
    Need(!Recover(ambiguous.app,policy,error),"Ambiguous modified target stops rollback");
    Need(ambiguous.Read(L"TuranBrowser.dll")=="modified after interrupted install","Ambiguous target preserved");ambiguous.Profiles();pass("tampered interrupted install stops without deleting files");
    Fixture success(root/L"successful-portable");stage=StageFixture(success,policy,key);
    Need(Apply(success.app,stage,policy,error),"Apply signed portable update: "+error);
    for(const auto& entry:success.payload)Need(success.Read(entry.first)==entry.second,"Installed payload bytes");success.Profiles();
    Need(detail::Plain(stage/L"backup"/L"TuranBrowser.exe",false),"Old verified executable retained");
    Need(!Stage(success.app,policy,success.OfferFor(key),stage,error,success.Fetcher()),"Installed signed receipt rejects replay");pass("successful portable replacement/receipt/retained backups");
    Fixture installed(root/L"successful-installed");Need(DeleteFileW((installed.app/L"portable.flag").c_str())!=FALSE,"Installed mode fixture");
    Need(Stage(installed.app,policy,installed.OfferFor(key,"installer"),stage,error,installed.Fetcher())&&Apply(installed.app,stage,policy,error),"Apply signed installed update: "+error);
    Need(installed.Read(L"Uninstall.exe")=="existing uninstaller"&&installed.Read(L"Profile/Cache/Cookies")=="fictional opaque profile cookie bytes","Installed user data/uninstaller preserved");pass("installed distribution keeps uninstaller/user data");
    Fixture busy(root/L"busy-target");stage=StageFixture(busy,policy,key);
    detail::Handle busy_file(CreateFileW((busy.app/L"TuranBrowser.exe").c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    Need(static_cast<bool>(busy_file)&&!Apply(busy.app,stage,policy,error),"Active runtime file blocks replacement");busy_file.Close();busy.Original();pass("busy runtime refusal preserves previous release");
    std::cout<<"PASS "<<checks<<" updater test groups; fixtures retained at "<<root.u8string()<<'\n';return 0;
  } catch(const std::exception& error){std::cerr<<"FAIL updater: "<<error.what()<<'\n';return 1;}
}
