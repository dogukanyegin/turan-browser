#include "ui/update_window.h"
#include "update/updater.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <list>
#include <thread>

namespace turan {
namespace {
struct State {
  HWND hwnd=nullptr;std::filesystem::path app,stage;UpdatePrepared prepared;
  update::Policy policy;update::Offer offer;bool turkish=false,offered=false,staged=false;
  std::atomic<bool> busy{false},finished{false};bool success=false;int task=0;
  std::atomic<bool> cancel{false};
  std::atomic<bool> worker_exception{false};
  std::mutex mutex;std::string result;
};
using StatePtr=std::shared_ptr<State>;
struct Worker {StatePtr state;std::thread thread;};
struct Registry {
  std::mutex mutex;std::list<Worker> workers;bool stopping=false;
  void Stop() {
    std::list<Worker> pending;
    {std::lock_guard<std::mutex> guard(mutex);stopping=true;pending.splice(pending.end(),workers);}
    for(auto& worker:pending)worker.state->cancel=true;
    for(auto& worker:pending)if(worker.thread.joinable())worker.thread.join();
  }
  ~Registry(){Stop();}
};
Registry& Threads(){static Registry registry;return registry;}
std::wstring Wide(const std::string& text) {
  int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
  std::wstring wide(n,L'\0');if(n)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),wide.data(),n);return wide;
}
const wchar_t* Text(const State& s,const wchar_t* tr,const wchar_t* en){return s.turkish?tr:en;}
void Status(State& s,const std::wstring& text){SetWindowTextW(GetDlgItem(s.hwnd,305),text.c_str());}
void Controls(State& s) {
  bool idle=!s.busy.load();EnableWindow(GetDlgItem(s.hwnd,301),idle&&update::Configured(s.policy));
  EnableWindow(GetDlgItem(s.hwnd,302),idle&&s.offered&&!s.staged);EnableWindow(GetDlgItem(s.hwnd,303),idle&&s.staged&&static_cast<bool>(s.prepared));
}
void Work(const StatePtr& s,int task) {
  if(s->busy.exchange(true))return;s->task=task;s->finished=false;
  s->cancel=false;
  s->worker_exception=false;
  if(task==1){s->offered=false;s->staged=false;s->offer={};s->stage.clear();}
  Controls(*s);
  Status(*s,Text(*s,L"İmzalı güncelleme doğrulanıyor…",L"Verifying the authenticated update…"));
  auto& registry=Threads();std::lock_guard<std::mutex> guard(registry.mutex);
  if(registry.stopping){s->busy=false;Status(*s,Text(*s,L"Tarayıcı kapanıyor.",L"The browser is shutting down."));Controls(*s);return;}
  try {
    // Allocate the registry node before creating its thread. If thread creation
    // fails, the node contains a non-joinable thread and can be erased safely.
    registry.workers.emplace_back();auto worker=std::prev(registry.workers.end());worker->state=s;
    try {
      worker->thread=std::thread([s,task] {
        std::string error;bool ok=false;auto cancelled=[s](){return s->cancel.load();};
        try {ok=task==1?update::Check(s->app,s->policy,s->offer,error,cancelled):update::Stage(s->app,s->policy,s->offer,s->stage,error,{},cancelled);}
        catch(...) {s->worker_exception=true;}
        {std::lock_guard<std::mutex> lock(s->mutex);s->success=ok;s->result=std::move(error);}
        s->finished.store(true,std::memory_order_release);
      });
    } catch(...) {registry.workers.erase(worker);throw;}
  } catch(...) {
    s->busy=false;Status(*s,Text(*s,L"Güncelleme iş parçacığı başlatılamadı.",L"The update worker could not be started."));Controls(*s);
  }
}
LRESULT CALLBACK Procedure(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam) {
  auto holder=reinterpret_cast<StatePtr*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
  if(message==WM_NCCREATE) {
    holder=static_cast<StatePtr*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(holder));(*holder)->hwnd=hwnd;
    return DefWindowProcW(hwnd,message,wparam,lparam);
  }
  if(!holder)return DefWindowProcW(hwnd,message,wparam,lparam);auto s=*holder;
  switch(message) {
  case WM_CREATE: {
    auto control=[&](const wchar_t* cls,const wchar_t* label,DWORD style,int x,int y,int width,int height,int id) {
      auto child=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,x,y,width,height,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
      SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return child;
    };
    control(L"STATIC",Text(*s,L"Turan Browser — Güvenli güncelleme",L"Turan Browser — secure update"),0,20,16,570,28,300);
    control(L"BUTTON",Text(*s,L"Güncelleme denetle",L"Check for update"),WS_TABSTOP,20,54,180,32,301);
    control(L"BUTTON",Text(*s,L"İndir ve doğrula",L"Download and verify"),WS_TABSTOP,210,54,180,32,302);
    control(L"BUTTON",Text(*s,L"Kapat ve kur",L"Close and install"),WS_TABSTOP,400,54,180,32,303);
    control(L"EDIT",L"",ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_BORDER,20,104,560,176,305);
    control(L"STATIC",Text(*s,L"Profil verileri korunur. Yayıncı imzası ve SHA-256 doğrulanır. Authenticode imzası ayrı bir işlemdir.",
        L"Profiles are preserved. Publisher signatures and SHA-256 are verified. Windows Authenticode is a separate signature."),0,20,292,560,52,306);
    if(!update::Configured(s->policy))Status(*s,Text(*s,L"NorthByte yayıncı anahtarı ve HTTPS yayın adresi henüz yapılandırılmadı. Doğrulanmamış paketler yüklenmez.",
        L"A NorthByte publisher public key and HTTPS release endpoint have not been provisioned. Unauthenticated packages cannot be installed."));
    else Status(*s,Wide("Channel: "+s->policy.channel+"\r\nManifest: "+s->policy.manifest_url));
    Controls(*s);SetTimer(hwnd,1,100,nullptr);return 0;
  }
  case WM_COMMAND:
    if(HIWORD(wparam)==BN_CLICKED) {
      if(LOWORD(wparam)==301)Work(s,1);
      else if(LOWORD(wparam)==302&&s->offered)Work(s,2);
      else if(LOWORD(wparam)==303&&s->staged&&s->prepared&&!s->busy) {
        auto callback=s->prepared;auto stage=s->stage;DestroyWindow(hwnd);callback(stage);
      }
    }
    return 0;
  case WM_TIMER:
    if(s->busy&&s->finished.load(std::memory_order_acquire)) {
      std::lock_guard<std::mutex> lock(s->mutex);s->busy=false;
      if(s->worker_exception)Status(*s,Text(*s,L"Güncelleme işlemi tamamlanamadı. Mevcut çalışma dosyaları ve profiller korundu.",
          L"The update worker failed. Existing runtime files and profiles were preserved."));
      else if(s->success) {
        if(s->task==1) {
          s->offered=true;auto v=s->offer.manifest.version;
          Status(*s,Wide("Authenticated release: "+std::to_string(v.major)+"."+std::to_string(v.minor)+"."+std::to_string(v.patch)+"\r\nFiles: "+
              std::to_string(s->offer.manifest.files.size())+"\r\nSequence: "+std::to_string(s->offer.manifest.sequence)));
        } else {s->staged=true;Status(*s,Text(*s,L"Güncelleme indirildi ve doğrulandı. Kurmak için Turan pencerelerini kapatın. Profiller güncelleme paketine dahil edilmez.",
            L"The update was downloaded and verified. Close Turan windows before installation. Profiles are excluded from update payloads."));}
      } else Status(*s,Wide(s->result));Controls(*s);
    }
    return 0;
  case WM_CLOSE:
    s->cancel=true;
    DestroyWindow(hwnd);return 0;
  case WM_NCDESTROY:
    s->cancel=true;
    KillTimer(hwnd,1);SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);delete holder;return DefWindowProcW(hwnd,message,wparam,lparam);
  }
  return DefWindowProcW(hwnd,message,wparam,lparam);
}
}
void WaitForUpdateWorkers(){Threads().Stop();}
HWND ShowUpdateWindow(HWND owner,const std::filesystem::path& app,bool turkish,UpdatePrepared prepared) {
  static const wchar_t cls[]=L"TuranSecureUpdateWindow";
  WNDCLASSEXW wc={sizeof wc};wc.lpfnWndProc=Procedure;wc.hInstance=GetModuleHandleW(nullptr);wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);wc.lpszClassName=cls;
  if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return nullptr;
  auto state=std::make_shared<State>();state->app=app;state->turkish=turkish;state->prepared=std::move(prepared);state->policy=update::PublisherPolicy();
  auto holder=new StatePtr(state);
  auto hwnd=CreateWindowExW(WS_EX_DLGMODALFRAME,cls,L"Turan Browser - secure update",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
      CW_USEDEFAULT,CW_USEDEFAULT,620,390,owner,nullptr,GetModuleHandleW(nullptr),holder);
  if(!hwnd&&state->hwnd==nullptr)delete holder;
  return hwnd;
}
}
