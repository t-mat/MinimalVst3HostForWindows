pushd .
cd /d "%~dp0"
curl.exe -JOL https://github.com/midilab/jc303/releases/download/v0.12.3/jc303-0.12.3-windows_x64-plugins.zip
tar.exe xvf jc303-0.12.3-windows_x64-plugins.zip
ren JC-303_Windows_X64-0.12.3 JC-303_Windows_X64
popd
