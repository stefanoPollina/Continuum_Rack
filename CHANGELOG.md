# Changelog

## 0.2.0 — 2026-11-26

- **WEB RACK view:** the Continuum Rack page itself runs in the plugin window (WebView2 / WKWebView). Its Web Audio engine is replaced by a bridge to the C++ engine, and scopes, meters, LEDs, the playhead and host automation are fed back to it. CORE SAVE opens a native save dialog.
- **NATIVE RACK view:** the full rack redrawn in JUCE on the web layout, with skins, knobs, cables (drag to patch, pull plugs, right-click to clear), SEQ 8×64 editing, DSP EDITOR, OCTOSCOPE, SCENES, KEYS and ENGINE ATLAS.
- A switch between the views is saved with the project. The old compact panel is gone.
- Engine: notes from the editor (keys, audition, PLAY BANK, factory drones), panic, octoscope taps, and RUN SEQ while the host is stopped.

## 0.1.0 — 2026-11-26

First source build of the Continuum Rack as a DAW instrument plugin (VST3, standalone; AU on macOS).

- C++ port of the web app's audio engine: 8 engine types, Mosaic wavetables, bank strips and inserts, forward sends, global LFO, master drive, echo and compressor.
- 8 × 64-step sequencer with arp, synced to the host transport, with an internal clock as fallback.
- MIDI input, gated or one-shot, to every engine or to one bank.
- All 48 factory scenes as host programs; web-app rack patches (`continuum-synth-p01`) load and save.
- 49 automatable parameters; full project state recall.
- Native rack-style editor: scene browser, clock, master, echo, LFO, router, MIDI, 8 bank strips with engine slots, meters and step view.
