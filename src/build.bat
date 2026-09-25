@echo off
rem Builds NovaOverlay.exe (static CRT, no runtime dependencies) into the parent folder.
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
cl /nologo /utf-8 /O2 /MT /EHsc /W3 /DUNICODE /D_UNICODE /DNDEBUG nova_overlay.cpp ^
   /Fe:..\NovaOverlay.exe /Fo:%TEMP%\nova_overlay.obj ^
   /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib gdiplus.lib dwmapi.lib shlwapi.lib ole32.lib winmm.lib shell32.lib
exit /b %ERRORLEVEL%
