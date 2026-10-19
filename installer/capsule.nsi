; Capsule installer (NSIS). Build: makensis installer\capsule.nsi  ->  build\CapsuleSetup.exe
; Per-user install, no admin rights: Capsule keeps ROMs, saves and settings next to capsule.exe,
; so it installs to a folder the user can write to.

Unicode true
!include "MUI2.nsh"

!define APP       "Capsule"
!define VERSION   "1.0.0"
!define EXE       "capsule.exe"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP}"
!define BUILD     "..\build"

Name "${APP}"
OutFile "${BUILD}\CapsuleSetup.exe"
InstallDir "$LOCALAPPDATA\Programs\${APP}"
InstallDirRegKey HKCU "${UNINSTKEY}" "InstallLocation"
RequestExecutionLevel user
SetCompressor /SOLID lzma
BrandingText "${APP} ${VERSION}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${APP}"
VIAddVersionKey "FileDescription" "${APP} installer"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "Unofficial fan project"

!define MUI_ICON   "capsule.ico"
!define MUI_UNICON "capsule.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TEXT "This will install ${APP} ${VERSION}, a launcher and emulator for the Pokemon series (Gen I-IX).$\r$\n$\r$\nNo games are included: you add ROMs dumped from your own cartridges.$\r$\n$\r$\nCapsule is an unofficial fan project, not affiliated with Nintendo, Game Freak, Creatures or The Pokemon Company."
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Start ${APP}"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

; Files in use can't be replaced, so ask the user to close a running Capsule (SDL2 window class "SDL_app").
!macro WaitForAppClosed
  wait:
    FindWindow $0 "SDL_app" "${APP}"
    StrCmp $0 0 done
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "${APP} is running. Close it, then click Retry." /SD IDCANCEL IDRETRY wait
    Abort
  done:
!macroend

Section "${APP}" SecMain
  SectionIn RO
  !insertmacro WaitForAppClosed
  SetOutPath "$INSTDIR"
  File "${BUILD}\${EXE}"
  File "THIRD_PARTY.txt"
  File "/oname=README.txt" "..\README.md"
  SetOutPath "$INSTDIR\cores"
  File "${BUILD}\cores\*.dll"
  SetOutPath "$INSTDIR"

  WriteUninstaller "$INSTDIR\uninstall.exe"
  CreateShortcut "$SMPROGRAMS\${APP}.lnk" "$INSTDIR\${EXE}"

  WriteRegStr   HKCU "${UNINSTKEY}" "DisplayName"     "${APP}"
  WriteRegStr   HKCU "${UNINSTKEY}" "DisplayVersion"  "${VERSION}"
  WriteRegStr   HKCU "${UNINSTKEY}" "Publisher"       "${APP}"
  WriteRegStr   HKCU "${UNINSTKEY}" "DisplayIcon"     "$INSTDIR\${EXE}"
  WriteRegStr   HKCU "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr   HKCU "${UNINSTKEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoRepair" 1
  SectionGetSize ${SecMain} $0
  WriteRegDWORD HKCU "${UNINSTKEY}" "EstimatedSize" $0
SectionEnd

Section "Desktop shortcut" SecDesktop
  CreateShortcut "$DESKTOP\${APP}.lnk" "$INSTDIR\${EXE}"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "The app and its emulation cores (GB/GBC/GBA, DS, 3DS)."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Put a ${APP} shortcut on the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  !insertmacro WaitForAppClosed
  Delete "$INSTDIR\${EXE}"
  Delete "$INSTDIR\THIRD_PARTY.txt"
  Delete "$INSTDIR\README.txt"
  Delete "$INSTDIR\cores\*.dll"
  RMDir  "$INSTDIR\cores"
  Delete "$INSTDIR\uninstall.exe"
  Delete "$SMPROGRAMS\${APP}.lnk"
  Delete "$DESKTOP\${APP}.lnk"
  DeleteRegKey HKCU "${UNINSTKEY}"

  ; ROM copies, saves, save states and settings live in the install folder. Keep them unless asked.
  ; (Games that were linked rather than copied are never touched: only their link files are here.)
  IfFileExists "$INSTDIR\*.*" 0 gone
  MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 \
    "Also delete your games, saves, save states and settings in$\r$\n$INSTDIR ?$\r$\n$\r$\nThis cannot be undone." /SD IDNO IDYES wipe
  RMDir "$INSTDIR" ; only removed if empty
  Goto gone
  wipe:
    RMDir /r "$INSTDIR"
  gone:
SectionEnd
