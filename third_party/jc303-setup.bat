setlocal enabledelayedexpansion
pushd .
cd /d "%~dp0"
if exist JC-303_Windows_X64\VST3\JC303.vst3\Contents\x86_64-win\JC303.vst3 ( goto :OK )
if exist JC-303_Windows_X64 ( rmdir /S /Q JC-303_Windows_X64 )
if not exist jc303-0.12.3-windows_x64-plugins.zip ( curl.exe -JOL https://github.com/midilab/jc303/releases/download/v0.12.3/jc303-0.12.3-windows_x64-plugins.zip )
if not exist jc303-0.12.3-windows_x64-plugins.zip ( goto :ERROR )
tar.exe xvf jc303-0.12.3-windows_x64-plugins.zip
ren JC-303_Windows_X64-0.12.3 JC-303_Windows_X64
if not exist JC-303_Windows_X64\VST3\JC303.vst3\Contents\x86_64-win\JC303.vst3 ( goto :ERROR )

:OK
exit /b 0

:ERROR
exit /b 1
