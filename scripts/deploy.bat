@if not defined _echo echo off
setlocal

rem Deploys the built DLL next to gamemd.exe so that Syringe picks it up.
rem
rem Usage: scripts\deploy.bat [Debug|Release] [game directory]
rem Defaults: Release, D:\Games\Ra2

cd /D "%~dp0"
cd ..

set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Release

set GAMEDIR=%~2
if "%GAMEDIR%"=="" set GAMEDIR=D:\Games\Ra2

if not exist "%CONFIG%\HAres.dll" (
  echo ERROR: "%CONFIG%\HAres.dll" not found - build it first.
  exit /b 1
)

if not exist "%GAMEDIR%\gamemd.exe" (
  echo ERROR: "%GAMEDIR%\gamemd.exe" not found - pass the game directory as the second argument.
  exit /b 1
)

copy /Y "%CONFIG%\HAres.dll" "%GAMEDIR%\HAres.dll" >nul
if exist "%CONFIG%\HAres.pdb" copy /Y "%CONFIG%\HAres.pdb" "%GAMEDIR%\HAres.pdb" >nul

rem The launcher lives next to gamemd.exe so it can be double-clicked.
copy /Y "scripts\RunHAres.bat" "%GAMEDIR%\RunHAres.bat" >nul

rem Create a default config only if the user does not have one yet, so that
rem re-deploying never overwrites local settings.
if not exist "%GAMEDIR%\HAres.ini" (
  >"%GAMEDIR%\HAres.ini" echo [General]
  >>"%GAMEDIR%\HAres.ini" echo ShowWatermark=1
  >>"%GAMEDIR%\HAres.ini" echo VerboseLog=0
  >>"%GAMEDIR%\HAres.ini" echo WatermarkCorner=0
  echo Created default HAres.ini
)

echo Deployed %CONFIG%\HAres.dll + RunHAres.bat to %GAMEDIR%
endlocal
