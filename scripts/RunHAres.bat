@echo off
rem ===========================================================================
rem  RunHAres.bat - launch Yuri's Revenge with the HAres extension DLL loaded.
rem
rem  Windowed mode, Ares/Phobos logging on, intro logo skipped.
rem
rem  Game switches used:
rem    -WIN         windowed mode. The RA2 engine is too old for the modern
rem                 desktop DirectDraw setup - without this the game dies at
rem                 "CreateSurface failed with error code 80070057".
rem    -CD          tell the game the CD is present, skip the CD check
rem    -NOLOGO      skip the Westwood/EA intro movies
rem    -LOG         enable Ares/Phobos logging -> debug\debug.log
rem    -AI-CONTROL  enable the AI control debug feature
rem
rem  Add -LOG-CSF to GAMEARGS below if you also want the (very large)
rem  string-table load log.
rem
rem  IMPORTANT - Syringe argument syntax differs between the two Syringes:
rem    * SyringeEx  (the modern one, currently deployed) needs the game
rem      arguments after --args="..." . Anything else on the command line is
rem      treated as Syringe's OWN option and never reaches the game.
rem    * The original closed-source Syringe takes them as trailing arguments.
rem  This script defaults to the SyringeEx form. If you ever restore the
rem  original Syringe.exe, run:  RunHAres.bat /oldsyringe
rem
rem  Logs to check afterwards:
rem      HAres.log         this extension's own log
rem      syringe.log       DLL recognition, handshakes, hook count, exceptions
rem      debug\debug.log   Ares/Phobos log
rem ===========================================================================

setlocal
cd /D "%~dp0"

set SYRINGE=Syringe.exe
set GAME=gamemd.exe
set GAMEARGS=-WIN -CD -NOLOGO -LOG -AI-CONTROL

set STYLE=syringeex
set EXTRA=%*
if /I "%~1"=="/oldsyringe" (
  set STYLE=original
  set EXTRA=
)

if not exist "%SYRINGE%" (
  echo ERROR: "%SYRINGE%" not found.
  echo        Run this script from the game directory ^(it resolves the working
  echo        directory from its own location, so a shortcut is fine^).
  pause
  exit /b 1
)

if not exist "%GAME%" (
  echo ERROR: "%GAME%" not found next to this script.
  pause
  exit /b 1
)

if not exist "HAres.dll" (
  echo WARNING: HAres.dll is not present - the game will start without the HAres extension.
  echo          Deploy it with:  HAres\scripts\deploy.bat Release "%~dp0."
  echo.
)

rem Clear the previous run's logs so what you read afterwards is definitely this run.
if exist "HAres.log"       del /Q "HAres.log"
if exist "syringe.log"     del /Q "syringe.log"
if exist "debug\debug.log" del /Q "debug\debug.log"

echo Launching %GAME% via %SYRINGE% ^(windowed, logging, no logo^)...

if "%STYLE%"=="original" (
  echo   argument style: original Syringe ^(trailing^)
  echo.
  "%SYRINGE%" "%GAME%" %GAMEARGS% %EXTRA%
) else (
  echo   argument style: SyringeEx ^(--args=^)
  echo.
  "%SYRINGE%" "%GAME%" --handshakes --args="%GAMEARGS% %EXTRA%"
)

echo.
echo %GAME% has exited. Logs written:
echo   %~dp0HAres.log
echo   %~dp0syringe.log
echo   %~dp0debug\debug.log
endlocal
