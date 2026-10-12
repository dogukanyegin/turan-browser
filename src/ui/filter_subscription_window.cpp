#include "ui/filter_subscription_window.h"
#include "core/model.h"
#include <shellapi.h>
#include <sstream>

namespace turan {
namespace {
constexpr wchar_t ClassName[]=L"TuranFilterSubscriptionsPanel";
constexpr int List=4111,Enabled=4112,Update=4113,Rollback=4114,Source=4115,Close=4116,License=4117,Details=4118;
struct Panel {
  std::shared_ptr<FilterSubscriptions> subscriptions;
  std::function<void()> changed;
  std::function<std::string()> publication_status;
  bool tr=false;
  bool* creation_accepted=nullptr;
  HWND window=nullptr,list=nullptr,enabled=nullptr,details=nullptr,update=nullptr,rollback=nullptr,source=nullptr,license=nullptr,close=nullptr;
  size_t index=0;
  std::wstring Text(const wchar_t* english,const wchar_t* turkish) const{return tr?turkish:english;}
  void Notify(){if(changed)changed();}
  void Error(const std::string& value){MessageBoxW(window,Wide(value).c_str(),Text(L"Turan Shield",L"Turan Shield").c_str(),MB_OK|MB_ICONERROR);}
  void Layout(){RECT r;GetClientRect(window,&r);UINT dpi=GetDpiForWindow(window);if(!dpi)dpi=96;auto scale=[&](int n){return MulDiv(n,dpi,96);};int w=r.right,h=r.bottom,p=scale(16),button=scale(100),gap=scale(8),line=scale(32);
    MoveWindow(list,p,p,w-2*p,line*6,TRUE);MoveWindow(enabled,p,p+line+gap,w-2*p,line,TRUE);MoveWindow(details,p,p+2*(line+gap),w-2*p,h-(p+2*(line+gap))-line-p-gap,TRUE);
    HWND buttons[]={update,rollback,source,license,close};for(int i=0;i<5;++i)MoveWindow(buttons[i],p+i*(button+gap),h-p-line,button,line,TRUE);
  }
  void Refresh(){auto statuses=subscriptions->Snapshot();if(index>=statuses.size())return;const auto& s=statuses[index];SendMessageW(enabled,BM_SETCHECK,s.enabled?BST_CHECKED:BST_UNCHECKED,0);EnableWindow(update,!subscriptions->Busy()&&s.enabled);EnableWindow(rollback,!subscriptions->Busy()&&s.cached);
    std::wostringstream out;out<<Wide(s.name)<<L"\r\n"<<Wide(s.url)<<L"\r\n"<<Wide(s.license)<<L"\r\n\r\n";
    out<<Text(L"Enabled: ",L"Etkin: ")<<(s.enabled?Text(L"Yes",L"Evet"):Text(L"No",L"Hayır"))<<L"    "<<Text(L"Cached: ",L"Önbellek: ")<<(s.cached?Text(L"Yes",L"Evet"):Text(L"No",L"Hayır"))<<L"\r\n";
    out<<Text(L"Network rules: ",L"Ağ kuralları: ")<<s.network<<L"    "<<Text(L"Cosmetic rules: ",L"Kozmetik kuralları: ")<<s.cosmetic<<L"\r\n";
    out<<Text(L"Unsupported rules skipped: ",L"Atlanan desteklenmeyen kurallar: ")<<s.unsupported<<L"\r\n"<<Text(L"Invalid rules skipped: ",L"Atlanan geçersiz kurallar: ")<<s.invalid<<L"\r\n";
    out<<Text(L"Bytes: ",L"Bayt: ")<<s.bytes<<L"    "<<Text(L"Updated (Unix seconds): ",L"Güncelleme (Unix saniye): ")<<s.updated<<L"\r\nSHA-256: "<<Wide(s.sha256)<<L"\r\n";
    if(s.updating)out<<Text(L"Downloading and validating...",L"İndiriliyor ve doğrulanıyor...")<<L"\r\n";
    if(s.recovered)out<<Text(L"Using a validated previous cache.",L"Doğrulanmış önceki önbellek kullanılıyor.")<<L"\r\n";
    if(!s.error.empty())out<<Text(L"Last result: ",L"Son sonuç: ")<<Wide(s.error)<<L"\r\n";
    if(publication_status){auto result=publication_status();if(!result.empty())out<<Text(L"Active policy: ",L"Etkin filtre politikası: ")<<Wide(result)<<L"\r\n";}
    out<<L"\r\n"<<Text(L"Turan supports a bounded ABP subset. Unsupported rule constraints are skipped whole; scriptlets and redirect execution are not enabled. Cache integrity hashes are not publisher signatures.",L"Turan sınırlı bir ABP alt kümesini destekler. Desteklenmeyen kural kısıtları bütünüyle atlanır; scriptlet ve yönlendirme çalıştırma etkin değildir. Önbellek bütünlük hash'leri yayıncı imzası değildir.");
    auto text=out.str();int count=GetWindowTextLengthW(details);std::wstring previous(count+1,L'\0');GetWindowTextW(details,previous.data(),count+1);previous.resize(count);if(previous!=text)SetWindowTextW(details,text.c_str());
  }
  void Command(int id){auto statuses=subscriptions->Snapshot();if(index>=statuses.size())return;auto status=statuses[index];std::string error;
    if(id==List){auto selected=SendMessageW(list,CB_GETCURSEL,0,0);if(selected>=0&&static_cast<size_t>(selected)<statuses.size())index=static_cast<size_t>(selected);Refresh();return;}
    if(id==Enabled){bool enable=SendMessageW(enabled,BM_GETCHECK,0,0)==BST_CHECKED;if(enable&&!status.enabled){
      auto message=Text(L"Enable this optional subscription?\r\n\r\n",L"Bu isteğe bağlı abonelik etkinleştirilsin mi?\r\n\r\n")+Wide(status.name+"\n"+status.url+"\n"+status.license)+Text(L"\r\n\r\nUpdates make HTTPS requests to this publisher using Windows TLS and direct DNS, outside the browser's Secure DNS/proxy settings. No browsing URLs, cookies or login credentials are sent. Valid lists refresh once per day; failed requests retry after one hour. The supported ABP subset skips unsupported/invalid rules. Disabled by default.",L"\r\n\r\nGüncellemeler bu yayıncıya Windows TLS ve doğrudan DNS üzerinden, tarayıcının Güvenli DNS/proxy ayarları dışında HTTPS isteği gönderir. Gezinilen URL'ler, çerezler veya oturum bilgileri gönderilmez. Geçerli listeler günde bir kez yenilenir; başarısız istekler bir saat sonra tekrar denenir. Desteklenen ABP alt kümesi desteklenmeyen/geçersiz kuralları atlar. Varsayılan olarak kapalıdır.");
      if(MessageBoxW(window,message.c_str(),Text(L"Optional filter subscription",L"İsteğe bağlı filtre aboneliği").c_str(),MB_YESNO|MB_ICONINFORMATION|MB_DEFBUTTON2)!=IDYES){Refresh();return;}
    }
      if(!subscriptions->SetEnabled(status.id,enable,enable,error))Error(error);else Notify();Refresh();return;
    }
    if(id==Update){if(!subscriptions->RefreshAsync(true))Error("No enabled subscription is due, or an update is already running");else Notify();Refresh();return;}
    if(id==Rollback){if(!subscriptions->Rollback(status.id,error))Error(error);else Notify();Refresh();return;}
    if(id==Source||id==License){auto url=id==Source?Wide(status.url):L"https://easylist.to/pages/licence.html";auto result=ShellExecuteW(window,L"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL);if(reinterpret_cast<INT_PTR>(result)<=32)Error("Windows could not open the selected official HTTPS page");return;}
    if(id==Close)DestroyWindow(window);
  }
};
LRESULT CALLBACK Procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
  auto* panel=reinterpret_cast<Panel*>(GetWindowLongPtrW(window,GWLP_USERDATA));
  if(message==WM_NCCREATE){panel=static_cast<Panel*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);panel->window=window;if(panel->creation_accepted)*panel->creation_accepted=true;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(panel));}
  if(!panel)return DefWindowProcW(window,message,wparam,lparam);
  switch(message){
    case WM_CREATE:{auto create=[&](const wchar_t* cls,const std::wstring& text,DWORD style,int id){auto child=CreateWindowExW(0,cls,text.c_str(),WS_CHILD|WS_VISIBLE|style,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return child;};
      panel->list=create(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,List);for(const auto& d:FilterSubscriptions::Catalog())SendMessageW(panel->list,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(Wide(d.name).c_str()));SendMessageW(panel->list,CB_SETCURSEL,0,0);
      panel->enabled=create(L"BUTTON",panel->Text(L"Enable selected subscription",L"Seçili aboneliği etkinleştir"),BS_AUTOCHECKBOX|WS_TABSTOP,Enabled);
      panel->details=create(L"EDIT",L"",ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP|WS_BORDER,Details);SendMessageW(panel->details,EM_SETLIMITTEXT,8192,0);
      panel->update=create(L"BUTTON",panel->Text(L"Update now",L"Şimdi güncelle"),WS_TABSTOP,Update);panel->rollback=create(L"BUTTON",panel->Text(L"Rollback",L"Geri al"),WS_TABSTOP,Rollback);panel->source=create(L"BUTTON",panel->Text(L"Source",L"Kaynak"),WS_TABSTOP,Source);panel->license=create(L"BUTTON",panel->Text(L"License",L"Lisans"),WS_TABSTOP,License);panel->close=create(L"BUTTON",panel->Text(L"Close",L"Kapat"),WS_TABSTOP,Close);
      panel->Layout();panel->Refresh();SetTimer(window,1,500,nullptr);return 0;
    }
    case WM_COMMAND:if(HIWORD(wparam)==BN_CLICKED||HIWORD(wparam)==CBN_SELCHANGE)panel->Command(LOWORD(wparam));return 0;
    case WM_TIMER:panel->Refresh();return 0;
    case WM_SIZE:panel->Layout();return 0;
    case WM_DPICHANGED:{auto r=reinterpret_cast<RECT*>(lparam);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);panel->Layout();return 0;}
    case WM_GETMINMAXINFO:{auto limit=reinterpret_cast<MINMAXINFO*>(lparam);UINT dpi=GetDpiForWindow(window);if(!dpi)dpi=96;limit->ptMinTrackSize={MulDiv(590,dpi,96),MulDiv(350,dpi,96)};return 0;}
    case WM_CLOSE:DestroyWindow(window);return 0;
    case WM_NCDESTROY:KillTimer(window,1);SetWindowLongPtrW(window,GWLP_USERDATA,0);delete panel;return DefWindowProcW(window,message,wparam,lparam);
  }return DefWindowProcW(window,message,wparam,lparam);
}
}
HWND OpenFilterSubscriptionWindow(HWND owner,std::shared_ptr<FilterSubscriptions> subscriptions,bool turkish,std::function<void()> changed,std::function<std::string()> publication_status){
  if(!IsWindow(owner)||!subscriptions)return nullptr;WNDCLASSW cls={};cls.lpfnWndProc=Procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=ClassName;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return nullptr;
  bool accepted=false;auto* panel=new Panel;panel->subscriptions=std::move(subscriptions);panel->changed=std::move(changed);panel->publication_status=std::move(publication_status);panel->tr=turkish;panel->creation_accepted=&accepted;
  auto window=CreateWindowExW(WS_EX_CONTROLPARENT,ClassName,turkish?L"Turan Shield — Filtre abonelikleri":L"Turan Shield — Filter subscriptions",WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,CW_USEDEFAULT,650,500,owner,nullptr,GetModuleHandleW(nullptr),panel);
  // WM_NCDESTROY owns deletion once WM_NCCREATE has accepted the state.
  if(window){panel->creation_accepted=nullptr;ShowWindow(window,SW_SHOWNORMAL);}else if(!accepted)delete panel;return window;
}
}
