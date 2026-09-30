@echo off
if exist "%~dp0NovaOBS-Setup.exe" (
    "%~dp0NovaOBS-Setup.exe" --portable-root "%~dp0"
)
pushd "%~dp0bin\64bit" || exit /b 1
obs64.exe %*
set "result=%errorlevel%"
popd
exit /b %result%
