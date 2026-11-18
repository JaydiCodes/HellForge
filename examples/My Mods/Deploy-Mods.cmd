@echo off
setlocal EnableExtensions DisableDelayedExpansion
set "destination=C:\Users\psige\Documents\HellForge\mods"
if not "%~1"=="" set "destination=%~f1"

set "deployed="
for /d %%D in ("%~dp0*") do (
  if exist "%%~fD\hellforge-mod.json" if not exist "%%~fD\hellforge-mod.json\" (
    robocopy "%%~fD" "%destination%\%%~nxD" /E /XJ /R:2 /W:1
    if errorlevel 8 goto :copyFailed
    set "deployed=1"
  )
)

if not defined deployed (
  echo No mod folders with a hellforge-mod.json file were found.
  exit /b 0
)

echo Mods deployed to "%destination%".
exit /b 0

:copyFailed
set "copyResult=%ERRORLEVEL%"
echo Mod deployment failed with Robocopy exit code %copyResult%. 1>&2
exit /b %copyResult%
