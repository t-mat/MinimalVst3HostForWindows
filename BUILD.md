Build
=====


Prerequisites
-------------

- Windows 11 (x64)


Building the project with portable MinGW
----------------------------------------

This method doesn't require any external development tools.

```bat
cmd.exe
pushd %PUBLIC%

mkdir MyTestVst3Host
cd    MyTestVst3Host

curl.exe -LOJ https://github.com/t-mat/MinimalVst3HostForWindows/archive/refs/heads/main.zip
tar.exe xvf MinimalVst3HostForWindows-main.zip
cd          MinimalVst3HostForWindows-main

call .\third_party\jc303-setup.bat
call .\build-mingw.bat

.\MinimalVst3HostForWindows.exe
```


Building the project with Visual Studio
---------------------------------------

Prerequisites: Visual Studio

```bat
cmd.exe
pushd %PUBLIC%

mkdir MyTestVst3Host
cd    MyTestVst3Host
curl.exe -LOJ https://github.com/t-mat/MinimalVst3HostForWindows/archive/refs/heads/main.zip
tar.exe xvf MinimalVst3HostForWindows-main.zip
cd          MinimalVst3HostForWindows-main

call .\third_party\jc303-setup.bat
call .\build.bat

.\MinimalVst3HostForWindows.exe
```


Building the project with `git`, `cmake` and Visual Studio
----------------------------------------------------------

Prerequisites: `git`, `cmake`, Visual Studio

```bat
cmd.exe
pushd %PUBLIC%

git clone https://github.com/t-mat/MinimalVst3HostForWindows.git
cd MinimalVst3HostForWindows

call .\third_party\jc303-setup.bat
cmake . -B build --fresh -A x64
cmake --build build --config Release

.\MinimalVst3HostForWindows.exe
```


Building the project in "Visual Studio Command Prompt"
------------------------------------------------------

Prerequisites: Visual Studio

Open the "x64 Native Tools Command Prompt for VS (2026)" from Windows Start Menu.  
Run the following commands to build and execute:

```bat
cmd.exe
pushd %PUBLIC%

curl.exe -LOJ https://github.com/t-mat/MinimalVst3HostForWindows/archive/refs/heads/main.zip
tar.exe xvf MinimalVst3HostForWindows-main.zip
cd          MinimalVst3HostForWindows-main

call .\third_party\jc303-setup.bat
cl /Fe:MinimalVst3HostForWindows /MT /std:c++20 /O2 /EHsc /I .\third_party\vst3sdk .\src\MinimalVst3HostForWindows.cpp

.\MinimalVst3HostForWindows.exe
```
