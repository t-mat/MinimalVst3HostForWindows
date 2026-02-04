pushd .
cd /d "%~dp0"

if not exist mingw64.7z ( curl.exe -o mingw64.7z -JL https://github.com/niXman/mingw-builds-binaries/releases/download/15.2.0-rt_v13-rev0/x86_64-15.2.0-release-win32-seh-ucrt-rt_v13-rev0.7z )
if not exist mingw64.7z ( echo "ERROR: Not found: mingw64.7z" && goto :ERROR )
if not exist mingw64\bin\c++.exe ( tar.exe xvf mingw64.7z )
if not exist mingw64\bin\c++.exe ( echo "ERROR: Not found: mingw64\bin\c++.exe" && goto :ERROR )

set "PATH=%CD%\mingw64\bin;%PATH%"
popd

echo c++ %*
     c++ %* || goto :ERROR

exit /b 0

:ERROR
exit /b 1
