@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"
for /F %%E in ('forfiles /m "%~nx0" /c "cmd /c echo 0x1b"') do set "_ESC=%%E"

set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%vswhere%" (
  echo Failed to find "vswhere.exe".  Please install the latest version of Visual Studio.
  goto :ERROR
)

set "vcvars64="
for /f "usebackq tokens=*" %%i in (
  `"%vswhere%" -latest -prerelease ^
               -products * ^
               -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ^
               -find **\VC\Auxiliary\Build\vcvars64.bat`
) do (
  set "vcvars64=%%i"
)
if "%vcvars64%" == "" (
  echo Failed to find Visual C++.  Please install the latest version of Visual C++.
  goto :ERROR
)

call "%vcvars64%" >nul || goto :ERROR

set "args=/nologo /Fe:MinimalVst3HostForWindows /MT /std:c++20 /O2 /EHsc /I .\third_party\vst3sdk .\src\MinimalVst3HostForWindows.cpp"

echo cl.exe %args%
     cl.exe %args% || goto :ERROR

dir /b MinimalVst3HostForWindows.exe || goto :ERROR

echo %_ESC%[92mOK%_ESC%[0m
exit /b 0


:ERROR
echo %_ESC%[91mERROR%_ESC%[0m
exit /b 1
