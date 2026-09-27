@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build_release.ps1" -Environment espwroom32 -Target Standalone
exit /b %ERRORLEVEL%
