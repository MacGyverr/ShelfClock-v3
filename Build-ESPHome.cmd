@echo off
setlocal
if not exist "%~dp0.tools\esphome\Scripts\python.exe" (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\setup_esphome.ps1"
  if errorlevel 1 exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build_release.ps1" -Environment espwroom32 -Target ESPHome
exit /b %ERRORLEVEL%
