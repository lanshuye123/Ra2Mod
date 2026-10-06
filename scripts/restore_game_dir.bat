@if not defined _echo echo off
setlocal

rem Restores the game directory to the state it was in before the build verification:
rem the original closed-source Syringe 0.7.3.0 and the original Phobos nightly.
rem
rem Usage: scripts\restore_game_dir.bat [game directory]
rem Defaults: D:\Games\Ra2

cd /D "%~dp0"
cd ..

set GAMEDIR=%~1
if "%GAMEDIR%"=="" set GAMEDIR=D:\Games\Ra2

set BK=%GAMEDIR%\_dsh_backup_orig

if not exist "%BK%\" (
  echo ERROR: backup directory "%BK%" not found - nothing to restore.
  exit /b 1
)

for %%f in (Syringe.exe Phobos.dll Phobos.pdb Ares.dll) do (
  if exist "%BK%\%%f" (
    copy /Y "%BK%\%%f" "%GAMEDIR%\%%f" >nul
    echo restored %%f
  )
)

rem HAres is our own DLL, so it is simply removed rather than restored.
if exist "%GAMEDIR%\HAres.dll" del /Q "%GAMEDIR%\HAres.dll"
if exist "%GAMEDIR%\HAres.pdb" del /Q "%GAMEDIR%\HAres.pdb"
if exist "%GAMEDIR%\HAres.log" del /Q "%GAMEDIR%\HAres.log"
echo removed HAres.dll / HAres.pdb / HAres.log

echo.
echo Game directory restored to the pre-verification state.
endlocal
