Minimal VST3 Host for Windows (C++20)
=====================================

A minimalist x64 VST 3.x host application for Windows 11, implemented in native C++20.


Prerequisites
-------------

- Windows 11 (x64)
- Visual Studio 2026
- `git`
- `cmake`


Building the Project
--------------------

```
cmd.exe

pushd %PUBLIC%

git clone https://github.com/t-mat/MinimalVst3HostForWindows.git
cd MinimalVst3HostForWindows

cmake . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release

.\MinimalVst3HostForWindows.exe
```
