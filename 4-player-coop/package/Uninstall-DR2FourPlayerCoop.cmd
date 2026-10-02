@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Uninstall-DR2FourPlayerCoop.ps1"
if errorlevel 1 (
  echo.
  echo Uninstall failed safely. Read the message above before changing any game files manually.
)
echo.
pause

