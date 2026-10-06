@if not defined _echo echo off

rem Builds the solution with the given configuration (Debug or Release).

rem Ensure we're in correct directory.
cd /D "%~dp0"

call run_msbuild /maxCpuCount /consoleloggerparameters:NoSummary /property:Configuration=%1 HAres.sln
