@echo off
setlocal EnableExtensions DisableDelayedExpansion
set "destination=C:\Users\psige\Documents\HellForge\mods"
if not "%~1"=="" set "destination=%~f1"

rem Build prebuilt mods before copying anything, so a failed build cannot deploy an old DLL.
if exist "%~dp0SpendMyResources\hellforge-mod.json" (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\scripts\Build-Editor.ps1" -SpendMyResources
  if errorlevel 1 goto :buildFailed
)

set "deployed="
for /d %%D in ("%~dp0*") do (
  if exist "%%~fD\hellforge-mod.json" if not exist "%%~fD\hellforge-mod.json\" (
    if /I "%%~nxD"=="SpendMyResources" (
      call :deployMod "%~dp0..\..\artifacts\SpendMyResources" "%%~nxD"
    ) else (
      call :deployMod "%%~fD" "%%~nxD"
    )
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

:deployMod
robocopy "%~1" "%destination%\%~2" /E /XJ /R:2 /W:1
exit /b %ERRORLEVEL%

:buildFailed
set "buildResult=%ERRORLEVEL%"
echo Mod build failed with exit code %buildResult%. No mods were deployed. 1>&2
exit /b %buildResult%

:copyFailed
set "copyResult=%ERRORLEVEL%"
echo Mod deployment failed with Robocopy exit code %copyResult%. 1>&2
exit /b %copyResult%
