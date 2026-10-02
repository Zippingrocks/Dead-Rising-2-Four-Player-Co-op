@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-DR2FourPlayerCoop.ps1"
if errorlevel 1 (
  echo.
  echo Installation failed. Read the message above; no unrelated proxy DLL was intentionally overwritten.
)
echo.
pause

