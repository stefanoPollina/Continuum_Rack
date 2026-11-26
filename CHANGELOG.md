# Changelog

## 0.1.0 — 2026-11-26

First source build of the Continuum Rack as a DAW instrument plugin (VST3, standalone; AU on macOS).

- C++ port of the web app's audio engine: 8 engine types, Mosaic wavetables, bank strips and inserts, forward sends, global LFO, master drive, echo and compressor.
- 8 × 64-step sequencer with arp, synced to the host transport, with an internal clock as fallback.
- MIDI input, gated or one-shot, to every engine or to one bank.
- All 48 factory scenes as host programs; web-app rack patches (`continuum-synth-p01`) load and save.
- 49 automatable parameters; full project state recall.
- Native rack-style editor: scene browser, clock, master, echo, LFO, router, MIDI, 8 bank strips with engine slots, meters and step view.
