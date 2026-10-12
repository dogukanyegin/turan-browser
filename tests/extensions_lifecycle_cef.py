"""Real engine extension manager and profile-scoped unpacked lifecycle QA.

The privileged WebUI is automated only by this loopback DevTools test. The app
exposes no developerPrivate bridge to web pages. Fresh owned fixture/profile.
"""
import argparse
import ctypes
import contextlib
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import socket
import shutil
import subprocess
import tempfile
import urllib.request
from ui_smoke import windows,u,CALLBACK
from thirdparty_extensions_cef import evaluate,until
from extension_manager_target import find_extensions_manager, summarize_debug_targets
from process_tree import hold_children,wait_children

ROOT=Path(__file__).resolve().parents[1]
u.EnumChildWindows.argtypes=[w.HWND,CALLBACK,w.LPARAM]
u.GetDlgCtrlID.argtypes=[w.HWND];u.GetDlgCtrlID.restype=ctypes.c_int
u.GetSystemMenu.argtypes=[w.HWND,w.BOOL];u.GetSystemMenu.restype=w.HMENU
u.GetMenuState.argtypes=[w.HMENU,w.UINT,w.UINT];u.GetMenuState.restype=w.UINT

def native_extensions_menu_ready(frame):
    """A bound native Chrome frame has Turan's real Extensions menu entry."""
    menu=u.GetSystemMenu(frame,False)
    return bool(menu) and u.GetMenuState(menu,0x1F20,0)!=0xFFFFFFFF

def controls(window):
    result=[]
    @CALLBACK
    def visit(hwnd,unused):
        name=ctypes.create_unicode_buffer(128);u.GetClassNameW(hwnd,name,128)
        result.append((hwnd,name.value,u.GetDlgCtrlID(hwnd)));return True
    u.EnumChildWindows(window,visit,0);return result

def confirm_fixture_removal(process_id,fixture_name):
    # Chromium always requests consent for uninstalling another extension,
    # including a management call from WebUI. Invoke only the native Views
    # button under a dialog named for this test's owned synthetic extension.
    # UI Automation remains scoped to windows belonging to the test process.
    handles=windows(process_id,'Chrome_WidgetWin_1')
    script=r'''
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$fixture=__FIXTURE__
$candidates=@()
foreach($handle in @(__HANDLES__)) {
  $root=[System.Windows.Automation.AutomationElement]::FromHandle([IntPtr]$handle)
  if($root.Current.ProcessId -ne __PID__) { throw 'Unexpected fixture window owner' }
  $all=$root.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition)
  foreach($element in $all) {
    if($element.Current.ControlType -ne [System.Windows.Automation.ControlType]::Button) { continue }
    if($element.Current.Name -notin @('Remove','Kaldır')) { continue }
    $ancestor=$element
    for($depth=0;$depth -lt 8 -and $null -ne $ancestor;$depth++) {
      $name=$ancestor.Current.Name
      if($name.Contains($fixture)) {
        $buttonName=$element.Current.Name
        $pattern=$element.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
        $pattern.Invoke()
        @{confirmed=$true;button=$buttonName;dialog=$name}|ConvertTo-Json -Compress
        exit 0
      }
      $ancestor=[System.Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($ancestor)
    }
    $candidates+=@{button=$element.Current.Name;ancestorName=$name}
  }
}
@{confirmed=$false;candidates=$candidates}|ConvertTo-Json -Compress
'''.replace('__FIXTURE__',"'"+fixture_name.replace("'","''")+"'").replace('__PID__',str(process_id)).replace('__HANDLES__',','.join(str(h) for h in handles))
    result=subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-Command',script],capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=15)
    if result.returncode:raise AssertionError('Fixture native UI Automation failed: '+result.stderr)
    value=json.loads(result.stdout)
    return value if value.get('confirmed') else None

def main(native_picker=False):
    exe=ROOT/'out-cef/app/Release/TuranBrowser.exe'
    proof={'runtime':'chrome-integrated','scope':'engine WebUI fixture lifecycle; startup unpacked loading by default; optional native picker; no store/CRX certification',
           'nativePickerRequested':native_picker,
           'binarySha256':hashlib.sha256(exe.with_suffix('.dll').read_bytes()).hexdigest()}
    process=None;handles=[];temporary=None
    try:
      with contextlib.nullcontext(tempfile.mkdtemp(prefix='turan-extension-lifecycle-')) as temporary:
        folder=Path(temporary)/'fixture';folder.mkdir();profile=Path(temporary)/'profile'
        manifest={'manifest_version':3,'name':'Turan isolated lifecycle fixture','version':'1.0.0',
          'permissions':['storage','scripting'],'host_permissions':['http://127.0.0.1/*'],
          'background':{'service_worker':'worker.js'},'action':{'default_popup':'popup.html'},
          'options_page':'options.html','incognito':'split'}
        (folder/'manifest.json').write_text(json.dumps(manifest))
        (folder/'worker.js').write_text('chrome.runtime.onInstalled.addListener(()=>chrome.storage.local.set({installed:true}));')
        (folder/'popup.html').write_text('<title>Turan lifecycle action</title><p>Actual fixture action popup</p>')
        (folder/'options.html').write_text('<title>Turan lifecycle options</title><p>Actual fixture options</p>')
        with socket.socket() as free:free.bind(('127.0.0.1',0));port=free.getsockname()[1]
        process=subprocess.Popen([str(exe),'--profile='+str(profile),'--turan-runtime=chrome-integrated',
            '--remote-debugging-port='+str(port),'--url=http://127.0.0.1:9/']+([] if native_picker else ['--load-extension='+str(folder)]),cwd=exe.parent)
        def targets():
            try:return json.load(urllib.request.urlopen('http://127.0.0.1:'+str(port)+'/json/list',timeout=2))
            except OSError:return []
        controller=until(lambda:windows(process.pid,'TuranWindow'))[0]
        # The controller is constructed before asynchronous CEF browser creation.
        # A command sent at HWND creation can be ignored because no browser exists.
        initial=until(lambda:next((r for r in targets() if r['type']=='page'
            and r['url'].startswith('http://127.0.0.1:9/')),None))
        until(lambda:evaluate(initial,'document.readyState === "complete"'))
        # A rendered page alone does not establish that the Chromium frame's
        # Turan menu subclass is bound. Exercise the *visible* frame command
        # after its native Extensions menu is registered (not the hidden
        # controller, not a CDP-created privileged page).
        frame=until(lambda:next((h for h in windows(process.pid,'Chrome_WidgetWin_1')
            if u.IsWindowVisible(h) and native_extensions_menu_ready(h)),None))
        proof['nativeExtensionsMenuRegistered']=True
        proof['nativeCommandTransport']='Chrome frame WM_SYSCOMMAND 0x1F20'
        proof['nativeCommandSent']=True
        u.SendMessageW(frame,0x112,0x1F20,0)
        # Chromium can expose chrome://extensions with or without a trailing
        # slash, query or fragment. Accept only actual page targets at this
        # privileged origin; never create it artificially in the test.
        manager=until(lambda:find_extensions_manager(targets()))
        proof['managerTargetValidated']=True
        proof['nativeMenuOpensEngineManager']=until(lambda:evaluate(manager,'!!chrome.developerPrivate && !!document.querySelector("extensions-manager")'))
        evaluate(manager,'chrome.developerPrivate.updateProfileConfiguration({inDeveloperMode:true});true')
        proof['developerMode']=evaluate(manager,'chrome.developerPrivate.getProfileConfiguration()')['inDeveloperMode']
        assert proof['developerMode']
        if native_picker:
            evaluate(manager,'globalThis.turanInstall=null;chrome.developerPrivate.loadUnpacked({failQuietly:true,populateError:true}).then(r=>globalThis.turanInstall={ok:true,result:r}).catch(e=>globalThis.turanInstall={ok:false,error:String(e)});true')
            # Folder-picker and file-picker control IDs differ on Windows versions.
            # Only use the filename/folder edit adjacent to static label 1090;
            # do not use the address/search-band edit (41477).
            try:
                dialog,field=until(lambda:next(((dialog,h) for dialog in windows(process.pid,'#32770')
                    for h,name,id in controls(dialog) if name=='Edit' and id in (1148,1152)
                    and u.IsWindowVisible(h) and u.IsWindowEnabled(h)),None))
            except AssertionError:
                proof['folderDialogCandidates']=[{'controls':[{'class':name,'id':id} for h,name,id in controls(dialog)]}
                    for dialog in windows(process.pid,'#32770')]
                raise
            fields=controls(dialog);proof['folderDialogControls']=[{'class':name,'id':id} for h,name,id in fields]
            value=ctypes.create_unicode_buffer(str(folder))
            assert u.SendMessageW(field,0xC,0,ctypes.cast(value,ctypes.c_void_p).value)
            copied=ctypes.create_unicode_buffer(32768)
            u.SendMessageW(field,0xD,32768,ctypes.cast(copied,ctypes.c_void_p).value)
            assert copied.value==str(folder)
            button=next(h for h,name,id in controls(dialog) if name=='Button' and id==1)
            u.PostMessageW(button,0xF5,0,0)
            # A folder picker may navigate to a typed directory on the first click.
            # Confirm only when its address control reports our owned fixture path.
            def selected_folder():
                result=evaluate(manager,'globalThis.turanInstall')
                if result:return result
                for h,name,id in controls(dialog):
                    if name=='Edit' and id==41477:
                        text=ctypes.create_unicode_buffer(32768)
                        u.SendMessageW(h,0xD,32768,ctypes.cast(text,ctypes.c_void_p).value)
                        if text.value.rstrip('\\/').casefold()==str(folder).casefold():return 'confirm'
                return None
            selected=until(selected_folder,20)
            if selected=='confirm':u.PostMessageW(button,0xF5,0,0)
            result=until(lambda:evaluate(manager,'globalThis.turanInstall'),20)
            proof['nativeUnpackedInstall']=result
            assert result['ok'] and not result.get('result'),result
        else:proof['nativeUnpackedInstallTested']=False
        def info():
            return next((r for r in evaluate(manager,'chrome.developerPrivate.getExtensionsInfo({includeDisabled:true,includeTerminated:true})') if r['name']==manifest['name']),None)
        extension=until(info);identifier=extension['id'];proof['installedInfo']=extension
        assert extension['version']=='1.0.0' and extension['state']=='ENABLED'
        proof['realPermissionInfo']=bool(extension['permissions']['runtimeHostPermissions']['hosts'])
        assert proof['realPermissionInfo']
        worker=until(lambda:next((r for r in targets() if r['type']=='service_worker' and r['url'].startswith('chrome-extension://'+identifier+'/')),None))
        until(lambda:evaluate(worker,'typeof chrome !== "undefined" && !!chrome.action'))
        proof['actionPopupManifest']=evaluate(worker,'chrome.action.getPopup({})')
        assert proof['actionPopupManifest'].endswith('/popup.html')
        # Chromium's incognito update synchronously unloads/reloads the extension.
        # Combining it with pinning checks HasAction during that reload. The actual
        # settings UI exposes separate controls, so exercise separate operations.
        evaluate(manager,'chrome.developerPrivate.updateExtensionConfiguration({extensionId:'+json.dumps(identifier)+',pinnedToToolbar:true})')
        until(lambda:info().get('pinnedToToolbar'))
        proof['toolbarPinBeforeIncognitoReload']=True
        evaluate(manager,'chrome.developerPrivate.updateExtensionConfiguration({extensionId:'+json.dumps(identifier)+',incognitoAccess:true})')
        updated=until(lambda:(r if (r:=info()).get('pinnedToToolbar') and r['incognitoAccess']['isActive'] else None))
        proof['toolbarPinAndSeparateIncognitoPermission']=bool(updated)
        evaluate(manager,'chrome.developerPrivate.updateExtensionConfiguration({extensionId:'+json.dumps(identifier)+',incognitoAccess:false})')
        until(lambda:not info()['incognitoAccess']['isActive'])
        evaluate(manager,'chrome.management.setEnabled('+json.dumps(identifier)+',false)')
        until(lambda:info()['state']=='DISABLED')
        evaluate(manager,'chrome.management.setEnabled('+json.dumps(identifier)+',true)')
        until(lambda:info()['state']=='ENABLED');proof['enableDisable']=True
        manifest['version']='1.0.1';(folder/'manifest.json').write_text(json.dumps(manifest))
        evaluate(manager,'chrome.developerPrivate.reload('+json.dumps(identifier)+',{failQuietly:true});true')
        until(lambda:info()['version']=='1.0.1');proof['unpackedVersionUpdate']=True
        proof['externalPageHasNoDeveloperPrivate']=evaluate(until(lambda:next((r for r in targets() if r['type']=='page' and r['url'].startswith('http://127.0.0.1:9/')),None)),'!globalThis.chrome?.developerPrivate')
        assert proof['externalPageHasNoDeveloperPrivate']
        # Owned synthetic fixture only; retain the engine's actual consent UI.
        evaluate(manager,'globalThis.turanRemoved=null;chrome.management.uninstall('+json.dumps(identifier)+',{showConfirmDialog:true}).then(()=>globalThis.turanRemoved={ok:true}).catch(e=>globalThis.turanRemoved={ok:false,error:String(e)});true')
        proof['nativeRemovalConfirmation']=until(lambda:confirm_fixture_removal(process.pid,manifest['name']),25)
        removal=until(lambda:evaluate(manager,'globalThis.turanRemoved'))
        assert removal['ok'],removal
        assert info() is None
        proof['fixtureRemovalApi']=True
        proof['nativeRemovalConfirmationTested']=True
        handles=hold_children(process.pid);u.PostMessageW(controller,0x10,0,0)
        proof['exitCode']=process.wait(timeout=20);wait_children(handles);handles=[]
        assert proof['exitCode']==0
    except Exception as error:
        proof['failure']=repr(error)
        proof['browserExitCodeAtFailure']=process.poll() if process else None
        if process:
            try:
                proof['targetClassesAtFailure']=summarize_debug_targets(targets())
                proof['nativeChromeFrameCount']=len(windows(process.pid,'Chrome_WidgetWin_1'))
            except Exception as diagnostic_error:
                proof['diagnosticError']=type(diagnostic_error).__name__
        raise
    finally:
        if process and process.poll() is None:
            handles=handles or hold_children(process.pid)
            subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],capture_output=True);process.wait(timeout=15)
        if handles:wait_children(handles)
        if temporary and 'failure' not in proof:
            target=Path(temporary).resolve();parent=Path(tempfile.gettempdir()).resolve()
            assert target.parent==parent and target.name.startswith('turan-extension-lifecycle-')
            shutil.rmtree(target)
        elif temporary:proof['retainedFixture']=temporary
        (ROOT/'artifacts/extensions-lifecycle-cef.json').write_text(json.dumps(proof,indent=2)+'\n')
        print(json.dumps(proof,indent=2),flush=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--native-picker',action='store_true')
    main(parser.parse_args().native_picker)
