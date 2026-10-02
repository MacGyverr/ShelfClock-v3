@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build_release.ps1" -Environment espwroom32_isolation -Target Standalone
if errorlevel 1 exit /b %errorlevel%
endlocal
