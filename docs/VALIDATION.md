# Validation

## Where it was run

Linux x86-64, GCC 13.3, CMake 3.28, Ninja, JUCE 8.0.12 (commit `29396c22`), Release build with LTO. The standalone target was left out of that build because ALSA headers weren't available; it is built on Windows and macOS.

```
ctest: Rack.DSP ........ Passed   (89 checks)
       Rack.Processor .. Passed   (362 checks)
       Rack.VST3Host ... Passed
Tests/web/bridge_test.py  25 checks passed (headless Chrome 153, real page + bridge)
```

Linux has no WebKitGTK development headers here, so that build was made with `RACK_WEB_UI=OFF`. The WebView code (`WebRackView.cpp`, and `PluginEditor.cpp` with the web view enabled) was compiled as a syntax check against the JUCE 8.0.12 headers with `JUCE_WEB_BROWSER=1`, but has not been linked or run.

## Web rack view

- **Page side** (`Tests/web/bridge_test.py`): the real page plus `plugin-bridge.js`, in headless Chrome, with a mock of JUCE's `window.__JUCE__.backend`. Checked: hello on start; the initial rack is pushed once, with no traffic while idle; scene loads, knob edits and DSP-editor edits reach the plugin; RUN SEQ, computer keys, PLAY BANK, audition and CORE SAVE send the right events; plugin status lights the RUN LED, playhead, engine LEDs and MIDI note; host automation and plugin loads update the page without echoing back; PANIC and KILL keep the page live; no page errors.
- **Plugin side** (`RackWebBridge`, in the processor tests): a `rackState` payload captured from the real page (`Tests/Fixtures/page-rack-state.json`) applies to the rack and moves only the parameters that changed. Page edits are not echoed back. Host automation and host preset changes are sent to the page. Notes, drones, stop, panic, RUN and SAVE work. Status carries LEDs, meters and scopes. Malformed messages are ignored.

## Native rack view

The processor tests check that all 87 modules are laid out with over 600 controls, and that the following work: engine ON, inserts, host-parameter knobs, voice knobs, EDIT moving the DSP editor's focus, SEQ cells, cables (forward sends allowed, backward refused, LFO → MOD), ENGINE ATLAS load, SCENES, TRIG, STOP and RUN. Full-size snapshots were compared by eye with the web rack's own screenshots.

## Engine tests (`Tests/DSPTests.cpp`, no JUCE)

- **Tables:** the four Web Audio basic shapes peak at 1. The mip level used for a 5 kHz note at 48 kHz holds nothing above Nyquist. Identical 8-partial spectra share one table.
- **Mosaic bank:** loads all 131 tables with 17 frames each. Frames are normalised like a PeriodicWave and cached, and a truncated bank is rejected. A scan/sweep of 10 → 90 gives 4 frame hops and sounds.
- **Envelopes:** a one-shot voice ends at duration + release + 80 ms. A gated voice holds at sustain, then ends after its release. The sustain pedal holds released notes, and pedal-up releases them.
- **Host clock:** four quarter-note steps per bar at 120 BPM. A stopped transport triggers nothing. A loop jump back to 0 retriggers step 1. A step at ppq 1.0 lands on its sample, offset by the 6 ms compressor look-ahead.
- **Internal clock:** 16 steps per bar at 150 BPM; Run off stops it. Host mode with no host position falls back to Run.
- **Routing:** backward sends are dropped. A send makes the receiving bank stereo. A patched LFO reaches only its bank, and an unpatched LFO reaches all of them. A send carries audio, and unrelated banks stay silent.
- **All 8 engine types × 5 filter modes,** with resonance, fold, FM index, filter envelope, LFO depth and comb feedback all at maximum, at notes 24/60/108: output finite and bounded.
- **Stress:** all 64 engines, every step on with two-bar gates, 7 chained sends, maximum echo feedback, 240 BPM, 96 kHz, plus MIDI on top. The 256-voice pool fills, the output stays finite, and `process()` makes **zero heap allocations**.

## Processor tests (`Tests/ProcessorTests.cpp`, JUCE)

- **All 48 scenes,** each played for 4 bars with a running host transport at the scene's tempo. Each is audible and finite, and the tempo parameter follows the scene. With the output clip off, no scene went over 0 dBFS. The hottest was −1.3 dBFS (Deeper Dub), and the clip holds every scene at or under 0 dBFS.
- **CPU (this machine, one core):** 2.8–3.1 % on average across scenes; the worst scene used 10–11 %.
- **Host state:** every parameter, the rack, an edited slot, and the scene index and name are restored. The restored processor renders sample-identically. Malformed or truncated state leaves the rack unchanged.
- **Web patch files:** the web app's own `example-two-bank-send.json` imports with its send and LFO cables and enabled engines. Export then import is exact, and the export uses `continuum-synth-p01`. Wrong format ids, patches without 8 banks and invalid JSON are refused. All 48 scenes round-trip through the patch format.
- **Transport and MIDI:** stopped means silent, playing means the sequencer runs, and clock Off means silent. A MIDI note plays the rack with the sequencer off. A host block larger than the prepared size is rendered to its end. Mono output works.
- **Editor:** opens resizable at 1440 × 900, and the chosen view is saved with the project. Snapshots: `docs/native-rack.png`; `docs/web-rack.png` is the page with the bridge in Chrome.

## VST3 host check (`Tests/HostLoadCheck.cpp`)

The compiled `Continuum Rack.vst3` bundle was loaded through JUCE's VST3 hosting code:

- It scans as an instrument with 48 programs, the first being Squelch Dub.
- After prepare, it reports **288 samples** of latency at 48 kHz.
- Squelch Dub plays for 4.3 s under a running transport: rms 0.17, peak 0.57.
- State saves (144 KB) and restores; the editor opens.

The host sees 2,131 parameters. 49 are the rack's own; the rest are JUCE's standard VST3 MIDI-CC parameters, which is how hosts deliver CC64 (sustain) to a VST3.

## Not yet verified

- A Windows build with MSVC, and a macOS build. The code avoids compiler-specific features, but nothing has been compiled there yet.
- Scanning, transport sync, automation and project recall in **Ableton Live** and **FL Studio**. Use [DAW_CHECKLIST.md](DAW_CHECKLIST.md).
- A/B listening against the browser. No browser was available here to record a Web Audio reference, so the port is checked structurally (same graph, same constants, same automation curves), not sample by sample.
- The WEB RACK view in a real WebView2 / WKWebView plugin window: keyboard focus inside DAWs, WebView2 profile folder, and the event throughput of status updates.
- Both views on a real display: mouse interaction, file dialogs, HiDPI scaling.
- Steinberg's VST3 validator.
