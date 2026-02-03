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


How to Add VST3 Plugins
-----------------------

### Adding Plugins
To load plugins, add the paths of your `.vst3` files to the `global_pluginPaths` vector defined near the top of the source code. You can use absolute paths or relative paths combined with `localVst3Dir` or `commonVst3Dir`.

### Signal Flow & Processing Order
This application processes registered plugins strictly in linear order (from top to bottom) as defined in the list. The output (both audio and events) of the previous plugin is passed directly as the input to the next plugin in the chain.

### Recommended Order
To ensure the signal chain functions as intended, the following order is recommended:

- MIDI Events / Generators
  - Examples: Arpeggiators, Sequencers, Chord generators.
  - Role: Generates or modifies note data to be sent to the instrument.
- Instruments
  - Examples: Synthesizers, Samplers.
  - Role: Receives MIDI data and generates audio signals.
- Audio Effects
  - Examples: Distortion, Delay, Reverb, EQ.
  - Role: Processes the audio signal output from the instrument.
- Visualization / Analysis
  - Examples: VU Meters, Spectrum Analyzers, Oscilloscopes.
  - Role: Visualizes the final output signal without altering the sound.
