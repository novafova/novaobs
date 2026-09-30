Unicode true
Name "Nova OBS"
!ifndef STAGE
  !error "Pass /DSTAGE=<absolute package directory>"
!endif
!ifndef REDIST
  !error "Pass /DREDIST=<absolute Visual C++ runtime directory>"
!endif
!ifndef OUTPUT
  !error "Pass /DOUTPUT=<absolute installer path>"
!endif
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\NovaOBS"
RequestExecutionLevel user
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show
BrandingText "Nova OBS 32.2.2"
!include "MUI2.nsh"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\64bit\obs64.exe"
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchNova
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function LaunchNova
  SetOutPath "$INSTDIR\bin\64bit"
  Exec '"$INSTDIR\bin\64bit\obs64.exe"'
FunctionEnd

Section "Nova OBS" Main
  SetOutPath "$INSTDIR\bin"
  File /r "${STAGE}\bin\*.*"
  SetOutPath "$INSTDIR\data"
  File /r "${STAGE}\data\*.*"
  SetOutPath "$INSTDIR\obs-plugins"
  File /r "${STAGE}\obs-plugins\*.*"
  SetOutPath "$INSTDIR\nova-source"
  File /r "${STAGE}\nova-source\*.*"
  SetOutPath "$INSTDIR"
  File "${STAGE}\NovaOBS-Setup.exe"
  File "${STAGE}\Launch Nova OBS.cmd"
  File "${STAGE}\RegisterVirtualCamera.ps1"
  File "${STAGE}\NOVA-SOURCE.txt"
  File "${STAGE}\obs_portable_mode.txt"

  ; Preserve scenes and settings when the installer is run again for an update.
  SetOverwrite off
  SetOutPath "$INSTDIR\config"
  File /r "${STAGE}\config\*.*"
  SetOverwrite on

  InitPluginsDir
  SetRegView 64
  StrCpy $0 0
  ReadRegDWORD $0 HKLM "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64" "Installed"
  IntCmp $0 1 x64_done
    SetOutPath "$PLUGINSDIR"
    File /oname=vc_redist.x64.exe "${REDIST}\vc_redist.x64.exe"
    DetailPrint "Installing the bundled Microsoft Visual C++ x64 runtime"
    ExecWait '"$PLUGINSDIR\vc_redist.x64.exe" /install /quiet /norestart' $0
    IntCmp $0 0 x64_done
    IntCmp $0 3010 x64_done
    IntCmp $0 1638 x64_done
    MessageBox MB_ICONSTOP "The bundled Microsoft Visual C++ x64 runtime could not be installed (code $0)."
    Abort
  x64_done:
  SetRegView 32
  StrCpy $0 0
  ReadRegDWORD $0 HKLM "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x86" "Installed"
  IntCmp $0 1 x86_done
    SetOutPath "$PLUGINSDIR"
    File /oname=vc_redist.x86.exe "${REDIST}\vc_redist.x86.exe"
    DetailPrint "Installing the bundled Microsoft Visual C++ x86 runtime"
    ExecWait '"$PLUGINSDIR\vc_redist.x86.exe" /install /quiet /norestart' $0
    IntCmp $0 0 x86_done
    IntCmp $0 3010 x86_done
    IntCmp $0 1638 x86_done
    MessageBox MB_ICONSTOP "The bundled Microsoft Visual C++ x86 runtime could not be installed (code $0)."
    Abort
  x86_done:

  ExecWait '"$INSTDIR\NovaOBS-Setup.exe" --portable-root "$INSTDIR"' $0
  IntCmp $0 0 script_done
    MessageBox MB_ICONSTOP "Nova's clip script could not be activated (code $0). Close OBS and run this installer again."
    Abort
  script_done:

  ExecWait '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "$INSTDIR\RegisterVirtualCamera.ps1" -Root "$INSTDIR"' $0
  IntCmp $0 0 camera_done
    MessageBox MB_ICONEXCLAMATION "Nova OBS installed, but Windows did not register Virtual Camera. Other features work. You can register it later by running RegisterVirtualCamera.ps1 as Administrator."
  camera_done:

  CreateDirectory "$SMPROGRAMS\Nova OBS"
  ; NSIS stores $OUTDIR as the shortcut's working directory.
  SetOutPath "$INSTDIR\bin\64bit"
  CreateShortcut "$SMPROGRAMS\Nova OBS\Nova OBS.lnk" "$INSTDIR\bin\64bit\obs64.exe" "" "$INSTDIR\bin\64bit\obs64.exe" 0 SW_SHOWNORMAL "" "Launch Nova OBS"
  CreateShortcut "$SMPROGRAMS\Nova OBS\Uninstall Nova OBS.lnk" "$INSTDIR\Uninstall Nova OBS.exe"
  WriteUninstaller "$INSTDIR\Uninstall Nova OBS.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NovaOBSApp" "DisplayName" "Nova OBS"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NovaOBSApp" "DisplayVersion" "32.2.2"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NovaOBSApp" "Publisher" "Nova"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NovaOBSApp" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NovaOBSApp" "DisplayIcon" "$INSTDIR\bin\64bit\obs64.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NovaOBSApp" "UninstallString" '"$INSTDIR\Uninstall Nova OBS.exe"'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NovaOBSApp" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NovaOBSApp" "NoRepair" 1
SectionEnd

Section "Uninstall"
  IfFileExists "$INSTDIR\NOVA-SOURCE.txt" +3
    MessageBox MB_ICONSTOP "This folder does not contain a Nova OBS installation. Nothing was removed."
    Abort
  ExecWait '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "$INSTDIR\RegisterVirtualCamera.ps1" -Root "$INSTDIR" -Unregister' $0
  IntCmp $0 0 camera_removed
    MessageBox MB_ICONSTOP "Windows could not unregister Nova's Virtual Camera. Approve the Windows permission prompt and try uninstalling again."
    Abort
  camera_removed:
  Delete "$SMPROGRAMS\Nova OBS\Nova OBS.lnk"
  Delete "$SMPROGRAMS\Nova OBS\Uninstall Nova OBS.lnk"
  RMDir "$SMPROGRAMS\Nova OBS"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\NovaOBSApp"
  RMDir /r "$INSTDIR\bin"
  RMDir /r "$INSTDIR\data"
  RMDir /r "$INSTDIR\obs-plugins"
  RMDir /r "$INSTDIR\nova-source"
  Delete "$INSTDIR\NovaOBS-Setup.exe"
  Delete "$INSTDIR\Launch Nova OBS.cmd"
  Delete "$INSTDIR\RegisterVirtualCamera.ps1"
  Delete "$INSTDIR\NOVA-SOURCE.txt"
  Delete "$INSTDIR\obs_portable_mode.txt"
  Delete "$INSTDIR\Uninstall Nova OBS.exe"
  ; Leave config/obs-studio intact so reinstalling restores scenes and stream settings.
  RMDir "$INSTDIR"
SectionEnd
