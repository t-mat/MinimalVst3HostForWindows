Implementation Note
===================


Class Summaries & Notes
-----------------------

|Class       |Role            |Implementation Notes |
|:---        |:---            |:--- |
|`AppMain`   |Application Root|Manages the main message loop and audio thread. Handles the Ping-Pong buffer logic and Event List swapping. |
|`Vst3Plugin`|Plugin Wrapper  |Encapsulates the lifecycle of a single VST3 plugin (DLL load -> Init -> Process -> Terminate). Handles the complex "Component/Controller" connection handshake. |
|`Vst3Dll`   |DLL Loader      |RAII wrapper for `LoadLibrary` / `FreeLibrary`. Ensures `GetPluginFactory` is retrieved correctly. |
|`SpscQueue` |Lock-free Queue |Used for passing MIDI events from UI thread to Audio thread. Uses manual memory layout to prevent False Sharing. |
|`Wasapi`    |Audio Driver    |Minimal wrapper for Windows WASAPI (Shared Mode). Provides the callback for the audio thread. |
|`MyHost`    |Host Interface  |Implements `IHostApplication`. Minimal implementation required to pass `this` to plugins. Reference counting is dummy (always returns 1). |
