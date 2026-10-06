@if not defined _echo echo off

rem Removes all build output.

rem Ensure we're in correct directory.
cd /D "%~dp0"
cd ..

if exist Debug rmdir /S /Q Debug
if exist Release rmdir /S /Q Release
if exist .vs rmdir /S /Q .vs

echo Cleaned.
