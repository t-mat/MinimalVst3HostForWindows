# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

A minimalist x64 VST 3.x host for Windows 11, implemented entirely in a single C++20 file (`src/MinimalVst3HostForWindows.cpp`). Educational/self-study project — not for production use.

## Build Commands

All builds produce `MinimalVst3HostForWindows.exe` in the project root.

**MinGW (portable, no external tools needed):**
```bat
call .\build-mingw.bat
```

**Visual Studio (batch):**
```bat
call .\build.bat
```

**CMake (MSVC or CLion with MinGW/Clang):**
```bat
cmake . -B build --fresh -A x64
cmake --build build --config Release
```

**VS Command Prompt (direct cl.exe):**
```bat
cl /Fe:MinimalVst3HostForWindows /MT /std:c++20 /O2 /EHsc /W4 /I .\third_party\vst3sdk .\src\MinimalVst3HostForWindows.cpp
```

**Test plugin setup** (run before first build): `call .\third_party\jc303-setup.bat`

There are no tests or linting commands.

## Architecture

The entire application lives in `src/MinimalVst3HostForWindows.cpp` (~1050 lines) with these core classes:

- **AppMain** — Application root: main message loop, audio thread coordination, ping-pong buffer logic, event list swapping
- **Wasapi** — WASAPI shared-mode audio driver wrapper. Forces IEEE float 32-bit format and validates via `IsFormatSupported`. MMCSS priority boosting on a dedicated audio thread
- **Vst3Plugin** — Full VST3 plugin lifecycle manager (load → init → process → terminate). Handles Component/Controller split handshake with `isSameObject` guards, `IConnectionPoint` connect/disconnect, GUI windows, and keyboard-to-MIDI input mapping. Tracks `activated_`/`processing_` flags for safe partial-init cleanup
- **Vst3Dll** — RAII wrapper for LoadLibrary/FreeLibrary and GetPluginFactory retrieval
- **SpscQueue<T,N>** — Lock-free single-producer single-consumer queue for passing MIDI events between UI and audio threads; uses cache-line alignment to prevent false sharing (C4324 suppressed via `#pragma warning`)
- **MyHost / MyComponentHandler / MyPlugFrame / MySimpleEventList** — Minimal VST3 host interface implementations (IHostApplication, IComponentHandler, IPlugFrame, IEventList)

### Threading Model

Two threads: UI thread (STA, main message loop + keyboard input) and audio thread (MTA, WASAPI callback + VST processing). Communication is via lock-free `SpscQueue`. The audio thread uses MMCSS for real-time priority.

### Signal Flow

Plugins are processed in linear chain order as defined in `global_pluginPaths`. Each plugin's output (audio + events) feeds the next. Instruments sum their output with existing audio; effects replace it.

### Adding Plugins

Add `.vst3` file paths to `global_pluginPaths` near the top of the source file. Use absolute paths or relative paths with `localVst3Dir`/`commonVst3Dir`.

## Code Style

- `.clang-format`: LLVM-based, 120 column limit, 4-space indent, no tabs
- C++20 standard
- RAII and Steinberg `IPtr<>` smart pointers for resource management
- Logging via `MY_TRACE()` (green) and `MY_ERROR()` (red) macros to stderr
- GCC `-Wcast-function-type` avoided by casting `FARPROC` through `void*` (not direct function-to-function cast)
- `getBusCount` checked before `activateBus` calls

## Dependencies

- **VST3 SDK** — Headers in `third_party/vst3sdk/` (MIT license)
- **Windows APIs** — WASAPI, COM/OLE, MMCSS (`avrt.lib`)
- **MinGW64** — Portable toolchain in `third_party/mingw64.7z` (extracted by build-mingw.bat)
