Minimal VST3 Host for Windows (C++20)
=====================================

A minimalist x64 VST 3.x host application for Windows 11, implemented in C++20.

- This project is intended for self-study purposes only and is not for practical use.
- The source code is contained within a single file. [`src/MinimalVst3HostForWindows.cpp`](src/MinimalVst3HostForWindows.cpp)


Prerequisites
-------------

- Windows 11 (x64)
- Visual Studio 2026
  - Visual C++


Building the project
--------------------

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


Building the project with `git` and `cmake`
-------------------------------------------

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


How to Add VST3 Plugins
-----------------------

To load plugins, add the paths of your `.vst3` files to the `global_pluginPaths` vector
defined near the top of `src/MinimalVst3HostForWindows.cpp`.
You can use absolute paths or relative paths combined with `localVst3Dir` or `commonVst3Dir`.


### Signal Flow & Processing Order

This application processes registered plugins strictly in linear order (from top to bottom)
as defined in the `global_pluginPaths`.
The output (both audio and events) of the previous plugin is passed directly as the input to
the next plugin in the chain.


### Recommended Order

To ensure the signal chain functions as intended, the following order is recommended:

- MIDI Events / Generators (Arpeggiators, Sequencers, Chord generators)
- Instruments (Synthesizers, Samplers)
- Audio Effects (Distortion, Delay, Reverb, EQ)
- Visualization / Analysis (VU Meters, Spectrum Analyzers, Oscilloscopes)


Documents
---------

- [Build methods](BUILD.md)
- [Implementation Notes](IMPLEMENTATION_NOTES.md)


See Also
--------

- [VST 3 Plug-In SDK](https://github.com/steinbergmedia/vst3sdk) - Official Steinberg VST3 SDK
- [JC-303 Plugin](https://github.com/midilab/jc303) - Free Roland TB-303 clone plugin
