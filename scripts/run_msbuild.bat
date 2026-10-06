@if not defined _echo echo off

rem Executes MSBuild found via VS Locator, from inside the VS Developer Command Prompt.

rem Ensure we're in correct directory.
cd /D "%~dp0"

run_vsdevcmd.bat & cd .. & msbuild %*
