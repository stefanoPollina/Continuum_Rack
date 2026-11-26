# DAW checklist (Ableton Live and FL Studio)

Work through this once on Windows after the first build. Tick each line or note what happened.

## Install and scan

- [ ] Ableton: the plugin appears under Plug-Ins → VST3 → Continuum → Continuum Rack after a rescan
- [ ] FL Studio: Manage plugins → Find installed plugins lists Continuum Rack as a **generator** (instrument)
- [ ] It loads onto a MIDI track / Channel Rack slot without errors; the window opens at 1440 × 900 on the WEB RACK

## Views

- [ ] The plugin opens on the WEB RACK; it shows your rack with the current scene
- [ ] Turn a knob on the page: the sound changes; automate a bank level in the DAW: the page's knob moves
- [ ] Click NATIVE RACK: the same rack, same values; drag a cable from a bank OUT to a later IN; switch back: the cable is on the page too
- [ ] Close and reopen the plugin window, and save/reopen the project: the chosen view comes back
- [ ] Keys typed on the page (A W S E D …) play; note whether the DAW takes some keys first

## Transport sync

- [ ] Play from bar 1: the first step lands on the downbeat (compare with a kick on the grid)
- [ ] Change the DAW tempo while playing: the sequencer follows at once
- [ ] Loop a 2-bar region: the pattern restarts with the loop, no doubled or skipped first step
- [ ] Start from the middle of a bar: steps land on the 16th grid, not offset from the play position
- [ ] Stop: no new steps; tails ring out. The clock label reads **HOST SYNC** while playing
- [ ] FL Studio PAT and SONG modes both drive the sequencer
- [ ] Delay compensation: the plugin reports 6 ms latency; recorded audio lines up with the grid

## Scenes and presets

- [ ] The SCENES module and ◂ ▸ step through all 48 scenes; sound changes without stopping playback
- [ ] Ableton's preset list / FL's preset menu show the same 48 names
- [ ] After a scene change, level, pan, filter and master knobs move to the scene's values

## MIDI

- [ ] NOTES Gated: held notes sustain, release on note-off; the sustain pedal holds notes
- [ ] NOTES One-shot (web): each note plays 0.42 s regardless of length
- [ ] TARGET Bank N: only that bank plays
- [ ] CLOCK Off + MIDI clip: plays as an instrument with no sequencer

## Automation and recall

- [ ] Automate Bank 1 Filter and Master Level: smooth, no zipper noise
- [ ] Save the project, close, reopen: same scene, same knob values, same enabled slots
- [ ] Duplicate the track: both copies recall identically
- [ ] CORE ↓ SAVE in the plugin, then load that file in the web app (CORE ↑ LOAD): same rack
- [ ] In the web app, change a patch and save (CORE ↓ SAVE); CORE ↑ LOAD in the plugin: same rack

## Sound against the browser

For a few scenes, run the web app and the plugin at the same tempo and compare:

- [ ] Squelch Dub (acid, sends, echo)
- [ ] Driven Acid Roller (heaviest: 37 engines, a wavetable)
- [ ] Hypnotic Minimal (comb filter, wavetable)
- [ ] Melodic Techno (3000 % gates: long pads)
- [ ] Industrial (drive and compressor)

Note anything that sounds clearly different (level, brightness, timing, envelope shape).

## Load

- [ ] CPU meter for the heaviest scene (Driven Acid Roller) at 44.1/48 kHz, 128-sample buffer
- [ ] Run at 96 kHz and with a 32-sample buffer: no dropouts or errors
