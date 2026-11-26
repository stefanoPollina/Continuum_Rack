# Continuum Rack plugin: first build

This package holds **source code**, not a compiled plugin. A single build script makes the Windows VST3 and a standalone app.

## Build on Windows

1. Extract or clone into a normal local folder, for example `C:\AudioDev\Continuum-Rack`.
2. Install **Visual Studio 2022 Build Tools** (or Visual Studio 2022) with the **Desktop development with C++** workload. Include the MSVC x64 tools, a Windows SDK and the C++ CMake tools. CMake must be **3.24 or newer**.
3. Double-click **Build-Windows.cmd**. The first run downloads the pinned JUCE 8.0.12 source and the WebView2 SDK package (used for the web rack view), then compiles the plugin and runs the tests. Allow 5–15 minutes. If the WebView2 download is blocked, run `scripts\Build-Windows.ps1 -NoWebUI` instead; you then get the native rack only.
4. When the script reports success, look in `dist\`:
   - `Continuum Rack.vst3`: the plug-in, a folder
   - `Continuum Rack.exe`: a standalone app for a quick listen
5. Copy the whole `Continuum Rack.vst3` folder into `C:\Program Files\Common Files\VST3`.

The script writes only its own `build-windows` and `dist` folders. It doesn't install anything, and it doesn't change your saved PowerShell policy.

If you already have JUCE 8.0.12 somewhere, run `scripts\Build-Windows.ps1 -JucePath C:\path\to\JUCE` to skip the download.

## Load it in Ableton Live

1. **Settings → Plug-Ins:** turn on **Use VST3 Plug-in System Folder**, then **Rescan**.
2. Drag **Continuum Rack** (Plug-Ins → VST3 → Continuum) onto a MIDI track.
3. Press play in Live. The rack opens on **Squelch Dub**, and its sequencer follows Live's transport and tempo.
4. To change scenes, use the SCENES module, or Live's preset list in the device title bar.

## Load it in FL Studio

1. **Options → Manage plugins**. Make sure `C:\Program Files\Common Files\VST3` is in the search paths, then click **Find installed plugins**.
2. Add **Continuum Rack** to the Channel Rack (+ → Continuum Rack), or drag it from the Plugin Database.
3. Press play in **SONG** or **PAT** mode; the sequencer follows FL's song position.
4. To play it from the keyboard or the Piano roll, either set **CLOCK** to *Off* (pure instrument) or leave it running and play over the top.

## The two views

The plugin opens on the **WEB RACK**: your Continuum Rack page, running inside the plugin. To switch to the **NATIVE RACK** (the same rack drawn natively), click the orange button in the bottom-right corner. Both views edit the same rack and play through the same engine. The web view needs the Microsoft Edge WebView2 Runtime, which Windows 10 and 11 normally already have.

## First sound check

- Press play in the DAW. You should hear the scene's acid line, chords, sub and drums at the DAW's tempo.
- Stop the DAW. The sequencer stops, and echo tails ring out.
- Hold a MIDI note. With **NOTES: Gated**, every enabled engine plays and holds until you let go. Set **TARGET** to one bank to play only that bank.
- Load a patch saved from the web app with CORE **↑ LOAD**.
- Flip between WEB RACK and NATIVE RACK. Turn a knob in one view, and it is in the same place in the other.

Next, work through [docs/DAW_CHECKLIST.md](docs/DAW_CHECKLIST.md).

## What has and hasn't been verified

On Linux (GCC 13, JUCE 8.0.12), the following were built and passed: the engine tests, the processor tests (all 48 scenes, state recall, web patch files, MIDI, the web bridge, the native rack's controls) and a check that loads the compiled VST3 through JUCE's VST3 hosting code. The web page and its bridge were tested in headless Chrome, against a stand-in for the plugin side.

**Not yet done:** a Windows/MSVC compile, the WebView2 view inside a real plugin window, scanning and playback in Ableton and FL Studio, side-by-side listening against the browser, and mouse testing of both views on a real screen. Details are in [docs/VALIDATION.md](docs/VALIDATION.md).
