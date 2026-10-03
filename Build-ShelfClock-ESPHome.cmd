@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build_release.ps1" -Target ESPHome
exit /b %ERRORLEVEL%
