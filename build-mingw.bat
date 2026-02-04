@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"
for /F %%E in ('forfiles /m "%~nx0" /c "cmd /c echo 0x1b"') do set "_ESC=%%E"

set "args=-o MinimalVst3HostForWindows -std=c++23 src/MinimalVst3HostForWindows.cpp -I third_party/vst3sdk -lole32 -lavrt -static-libgcc -static-libstdc++"

echo .\third_party\mingw-c++.bat %args% %args%
call .\third_party\mingw-c++.bat %args% || goto :ERROR

dir /b MinimalVst3HostForWindows.exe || goto :ERROR

echo %_ESC%[92mOK%_ESC%[0m
exit /b 0


:ERROR
echo %_ESC%[91mERROR%_ESC%[0m
exit /b 1
