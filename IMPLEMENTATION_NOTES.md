Implementation Notes
====================


Class Summaries & Notes
-----------------------

|Class       |Role            |Implementation Notes |
|:---        |:---            |:--- |
|`AppMain`   |Application Root|Manages the main message loop and audio thread. Handles the Ping-Pong buffer logic and Event List swapping. |
|`MyHost`    |Host Interface  |Implements `IHostApplication`. Minimal implementation required to pass `this` to plugins. Reference counting is dummy (always returns 1). |
|`SpscQueue` |Lock-free Queue |Used for passing MIDI events from UI thread to Audio thread. Uses manual memory layout to prevent False Sharing. |
|`Vst3Dll`   |DLL Loader      |RAII wrapper for `LoadLibrary` / `FreeLibrary`. Ensures `GetPluginFactory` is retrieved correctly. |
|`Vst3Plugin`|Plugin Wrapper  |Encapsulates the lifecycle of a single VST3 plugin (DLL load -> Init -> Process -> Terminate). Handles the complex "Component/Controller" connection handshake. |
|`Wasapi`    |Audio Driver    |Minimal wrapper for Windows WASAPI (Shared Mode). Provides the callback for the audio thread. |


How to Add VST3 Plugins
-----------------------

### Adding Plugins
To load plugins, add the paths of your `.vst3` files to the `global_pluginPaths` vector
defined near the top of [`src/MinimalVst3HostForWindows.cpp`](src/MinimalVst3HostForWindows.cpp).
You can use absolute paths or relative paths combined with `localVst3Dir` or `commonVst3Dir`.

### Signal Flow & Processing Order
This application processes registered plugins strictly in linear order (from top to bottom)
as defined in the `global_pluginPaths`.
The output (both audio and events) of the previous plugin is passed directly as the input to
the next plugin in the chain.

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


VST3 Initialization Flow (`Vst3Plugin`)
---------------------------------------

Because the project does not use the SDK's `Hosting` helpers, the initialization in `Vst3Plugin::init`
is complex and verbose.

Component & Controller Split:  
VST3 plugins may separate DSP (Component) and GUI (Controller). The host must:
- Create the Component.
- Try to create the Controller from the Factory.
- Fallback:  
  If Factory creation fails, query the Component for `IEditController`.
- Connection:  
  Explicitly connect them via `IConnectionPoint` if they are separate objects.
