# Source provenance

## What was ported

The source is the Continuum Rack web app, `index.html` in `stefanoPollina/Hybrid` (Rack edition, 2026-11-17). It is a single 655 KB file, and its SHA-256 is recorded in `Resources/resource-metadata.json`.

| Web app (JavaScript) | Plugin (C++) |
|---|---|
| `voice()`: oscillators, wavetable hops, FM, shaper, resonator, additive, biquad / comb, LFO, envelope automation | `RackEngine::Voice::start` / `render` |
| `makeAudio()` / `updateAudio()`: bank strips, inserts, tremolo, pan, trim, master drive, delay, compressor | `RackEngine::Bus`, `RackEngine::Master`, `process()` |
| `applyMatrix()` / `applyLFO()` / `lfoTargets()`: cables | `computeRouting()` |
| `seqTick()`: steps, arp, gates, step filter, engine targets and masks | `RackEngine::runStep()` |
| `trigger()` / `onMidi()` | `RackEngine::midiNote()` |
| `normalize()`, `freshEdit()`, `defaultBanks()`, `initPattern()` | `normalise()`, `EngineEdit` defaults, `makeDefaultRack()` |
| `HWT` + Mosaic WT-Core (`renderFrame`, `fft`) | Run in Node by `scripts/extract-rack-data.cjs`; coefficients stored in `wavetables.bin.gz` |
| `SCENES` + `applyScene()` | Run in Node; each resulting rack state stored in `scenes.json.gz` |
| Save/load format `continuum-synth-p01` | `RackPatch.cpp` |

## How the data is extracted

`extract-rack-data.cjs` finds the page's three `<script>` blocks by their content and runs them in a Node sandbox with no DOM, stopping where the audio engine starts. Then, for each scene, it calls the page's own `applyScene()` and dumps `state`. For each Mosaic table, it builds the 17 frames with the page's `WT.buildTable`, applies the same FFT, and keeps the same 512 `real` / `imag` coefficients that `HWT.build` hands to `createPeriodicWave`. These are quantised to 16 bits per frame, peak-relative. The largest relative error is in `resource-metadata.json` (about 1.5 × 10⁻⁵).

`Tests/Fixtures/example-two-bank-send.json` is copied unchanged from `Hybrid/patches/`.

## Web Audio behaviour reproduced in C++

- **BiquadFilterNode:** Audio EQ Cookbook coefficients as the Web Audio spec defines them. Low- and high-pass Q is in dB; band-pass and notch Q is linear.
- **PeriodicWave:** the waveform is Σ real·cos + imag·sin, normalised by the peak of the full-band waveform. Built-in sine, square, sawtooth and triangle come from their Fourier series and are normalised the same way.
- **AudioParam automation:** `setValueAtTime`, `linearRampToValueAtTime`, `setTargetAtTime` and `exponentialRampToValueAtTime` are reproduced with the same values and times.
- **WaveShaperNode:** the page's `waveShape()` curve; input beyond ±1 clamps to the curve's ends.
- **StereoPannerNode:** the spec's separate mono and stereo equal-power laws. A bank that receives a send becomes a stereo bus, as it does in the browser.
- **DelayNode in a cycle:** at least one render quantum (128 frames).
- **DynamicsCompressorNode:** Chromium's knee curve, `k` solver, make-up gain (full-range gain ^ 0.6) and 6 ms pre-delay. The envelope uses a fixed attack and release.

## Not ported (yet)

The DSP editor, step editing, cable patching, the factory recipe builder, the mutator, the engine atlas browser, the scopes and the room-light and zoom options. These are editor features. Their results reach the plugin through patch files. `Resources/engine-patches.json` holds the 76 atlas patches for a future engine browser; the plugin doesn't use them yet.

## Third-party code

JUCE 8.0.12 is fetched at build time and not stored in this repo. As the web app's README says, the voice models are original Continuum designs, not ports of Open303, Surge XT, Dexed, Plaits or Vital.
