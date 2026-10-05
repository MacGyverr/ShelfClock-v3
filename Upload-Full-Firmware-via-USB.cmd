@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\upload_full_firmware.ps1" %*
exit /b %ERRORLEVEL%
