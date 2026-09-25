@echo off
rem Builds NovaOverlay.exe and NovaOBS-Setup.exe (static CRT, no runtime dependencies)
rem into the parent folder. The installer embeds the overlay and the Lua script,
rem so it is built last.
setlocal
set "VSDIR="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\nova_vsdir.txt"
  set /p VSDIR=<"%TEMP%\nova_vsdir.txt"
  del "%TEMP%\nova_vsdir.txt" >nul 2>&1
)
if not defined VSDIR (
  echo Visual Studio C++ build tools not found.
  exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
set "OBJ=%TEMP%\novaobs-build"
if not exist "%OBJ%" mkdir "%OBJ%"

rc /nologo /fo "%OBJ%\overlay.res" overlay.rc || exit /b 1
cl /nologo /utf-8 /O2 /MT /EHsc /W3 /DUNICODE /D_UNICODE /DNDEBUG nova_overlay.cpp "%OBJ%\overlay.res" ^
   /Fe:..\NovaOverlay.exe /Fo:"%OBJ%\nova_overlay.obj" ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTUAC:level='asInvoker' user32.lib gdi32.lib gdiplus.lib dwmapi.lib shlwapi.lib ole32.lib winmm.lib shell32.lib || exit /b 1

rc /nologo /fo "%OBJ%\setup.res" setup.rc || exit /b 1
cl /nologo /utf-8 /O2 /MT /EHsc /W3 /DUNICODE /D_UNICODE /DNDEBUG setup.cpp "%OBJ%\setup.res" ^
   /Fe:..\NovaOBS-Setup.exe /Fo:"%OBJ%\setup.obj" ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTUAC:level='asInvoker' user32.lib shell32.lib ole32.lib comctl32.lib advapi32.lib || exit /b 1

echo Built NovaOverlay.exe and NovaOBS-Setup.exe
