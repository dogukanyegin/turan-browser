Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
Name "${PRODUCT_NAME}"
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\${REGISTRATION_ID}"
RequestExecutionLevel user
SetCompressor /SOLID lzma
!define MUI_ABORTWARNING
!define MUI_ICON "..\..\resources\turan.ico"
!define MUI_UNICON "..\..\resources\turan.ico"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Turkish"
Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_OK "Turan Browser requires Windows x64."
    Abort
  ${EndIf}
FunctionEnd
Section "Turan Browser" Main
  SetOutPath "$INSTDIR"
  !include "install-files.nsh"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
  CreateShortcut "$SMPROGRAMS\${PRODUCT_NAME}\Turan Browser.lnk" "$INSTDIR\TuranStartup.exe"
  CreateShortcut "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${REGISTRATION_ID}" "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${REGISTRATION_ID}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${REGISTRATION_ID}" "Publisher" "NorthByte Norway Tec Ltd."
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${REGISTRATION_ID}" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${REGISTRATION_ID}" "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${REGISTRATION_ID}" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${REGISTRATION_ID}" "NoRepair" 1
SectionEnd
Section "Uninstall"
  # Delete only files this installer owns; never recursively remove user data.
  !include "owned-files.nsh"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR\locales"
  RMDir "$INSTDIR\resources"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\Turan Browser.lnk"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk"
  RMDir "$SMPROGRAMS\${PRODUCT_NAME}"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${REGISTRATION_ID}"
SectionEnd
