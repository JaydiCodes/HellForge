@echo off
setlocal
set "destination=C:\Users\psige\Documents\HellForge\mods"
if not "%~1"=="" set "destination=%~f1"

robocopy "%~dp0." "%destination%" /E /XJ /R:2 /W:1 /XF "%~f0"
set "copyResult=%ERRORLEVEL%"
if %copyResult% GEQ 8 (
  echo Mod deployment failed with Robocopy exit code %copyResult%. 1>&2
  exit /b %copyResult%
)

echo Mods deployed to "%destination%".
exit /b 0
