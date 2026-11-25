@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Run with the game closed. Optionally pass the path to the game's mods folder.
set "destination=C:\Users\psige\Documents\HellForge_0_5_0\mods"
if not "%~1"=="" set "destination=%~f1"

rem Build prebuilt mods before copying anything, so a failed build cannot deploy an old DLL.
set "artifacts=%~dp0..\..\artifacts"
set "unbalanceMod=JedsDead's Unbalance Pass"
call :buildPackage "SpendMyResources" -SpendMyResources "%artifacts%\SpendMyResources\spend_my_resources.dll"
if errorlevel 1 goto :buildFailed
call :buildPackage "%unbalanceMod%" -UnbalancePass "%artifacts%\UnbalancePass\unbalance_pass.dll"
if errorlevel 1 goto :buildFailed

set "deployed="
for /d %%D in ("%~dp0*") do (
  if exist "%%~fD\hellforge-mod.json" if not exist "%%~fD\hellforge-mod.json\" (
    if /I "%%~nxD"=="SpendMyResources" (
      call :deployMod "%artifacts%\SpendMyResources" "%%~nxD"
    ) else if /I "%%~nxD"=="%unbalanceMod%" (
      call :deployUnbalance "%artifacts%\UnbalancePass" "%%~nxD"
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

:buildPackage
if not exist "%~dp0%~1\hellforge-mod.json" exit /b 0
if exist "%~dp0%~1\hellforge-mod.json\" exit /b 0
echo Building "%~1"...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\scripts\Build-Editor.ps1" %2
if errorlevel 1 exit /b %ERRORLEVEL%
if not exist "%~3" (
  echo Expected mod DLL not found: "%~3". 1>&2
  exit /b 1
)
exit /b 0

:deployMod
robocopy "%~1" "%destination%\%~2" /E /XJ /R:2 /W:1
exit /b %ERRORLEVEL%

:deployUnbalance
call :deployMod "%~1" "%~2"
if errorlevel 8 exit /b %ERRORLEVEL%

rem Leftover src would make the launcher build and load the gameplay-only DLL.
set "oldSource=%destination%\%~2\src"
if not exist "%oldSource%\" exit /b 0
if exist "%~1\src\" exit /b 0
for %%B in ("%destination%\..\HellForge-mod-deploy-backups\UnbalancePass-%RANDOM%-%RANDOM%") do set "backup=%%~fB"
mkdir "%backup%"
if errorlevel 1 goto :backupFailed
move /Y "%oldSource%" "%backup%\src" >nul
if errorlevel 1 goto :backupFailed
echo Backed up old Unbalance Pass sources to "%backup%\src".
exit /b 0

:backupFailed
echo Could not back up "%oldSource%" to "%backup%\src". The menu-enabled upgrade is incomplete. 1>&2
exit /b 8

:buildFailed
set "buildResult=%ERRORLEVEL%"
echo Mod build failed with exit code %buildResult%. No mods were deployed. 1>&2
exit /b %buildResult%

:copyFailed
set "copyResult=%ERRORLEVEL%"
echo Mod deployment failed with exit code %copyResult%. 1>&2
exit /b %copyResult%
