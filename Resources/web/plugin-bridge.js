/* Continuum Rack · plugin bridge
 *
 * Injected after the web app's own scripts when index.html runs inside the plugin's
 * WebView. The page keeps its whole interface; its Web Audio engine is replaced:
 *   - every edit to `state` is sent to the plugin, whose C++ engine makes the sound in the DAW
 *   - keys, audition, PLAY BANK, RUN SEQ, PANIC and SAVE go to the plugin as events
 *   - meters, scopes, step playhead and engine LEDs are fed back from the plugin
 * In an ordinary browser window.__JUCE__ is absent and this script does nothing.
 */
(function () {
  'use strict';
  const backend = window.__JUCE__ && window.__JUCE__.backend;
  if (!backend || typeof backend.emitEvent !== 'function') return;
  window.CONTINUUM_PLUGIN = true;

  const emit = (id, payload) => { try { backend.emitEvent(id, payload); } catch (err) { console.error('continuum bridge', id, err); } };

  let sampleRate = 48000, running = false, lastTick = null, scopeBank = -1, lastSent = '', applying = false, pushQueued = false;
  const lastHits = new Array(64).fill(null);

  const fakeCtx = {
    state: 'running',
    get currentTime() { return performance.now() / 1000; },
    get sampleRate() { return sampleRate; },
    baseLatency: 0,
    resume: async () => {},
    close: async () => {}
  };
  const analyser = size => ({
    fftSize: size,
    data: new Float32Array(size),
    getFloatTimeDomainData(d) { d.set(this.data.length === d.length ? this.data : this.data.subarray(0, d.length)); }
  });
  // Stretches src (any length) across dst.
  function resampleInto(dst, src) {
    const n = dst.length, m = src.length;
    if (!m) { dst.fill(0); return; }
    for (let i = 0; i < n; i++) {
      const x = n > 1 ? i * (m - 1) / (n - 1) : 0, j = Math.floor(x), f = x - j;
      dst[i] = j + 1 < m ? src[j] + f * (src[j + 1] - src[j]) : src[m - 1];
    }
  }

  // ── state → plugin ─────────────────────────────────────────────────────────────
  function pushNow() {
    pushQueued = false;
    if (applying) return;
    const s = JSON.stringify(state);
    if (s === lastSent) return;
    lastSent = s;
    emit('rackState', { stateJson: s, sceneIndex: typeof ui.scene === 'number' ? ui.scene : -1 });
  }
  function schedulePush() { if (!pushQueued) { pushQueued = true; setTimeout(pushNow, 0); } }
  setInterval(pushNow, 120);   // catches every edit path, including ones that only touch `state`

  function setRun(on) {
    running = on;
    seqTimer = on ? 1 : null;
    leds();
    emit('rackRun', { run: on });
  }

  // ── the page's audio engine, replaced ──────────────────────────────────────────
  makeAudio = function () {
    if (ctx) return;
    ctx = fakeCtx;
    masterAnalyser = analyser(2048);
    bankAnalysers = Array.from({ length: 8 }, () => analyser(256));
    slotAnalysers = Array.from({ length: 8 }, () => Array.from({ length: 8 }, () => analyser(2048)));
    slotInputs = Array.from({ length: 8 }, (_, b) => Array.from({ length: 8 }, (_, s) => ({ bank: b, slot: s })));
    busNodes = [];
    ui.lfoKey = null; ui.tremKey = null; ui.idleDrawn = false;
  };
  ensure = async function () {
    makeAudio();
    if (!ui.audioSaid) { ui.audioSaid = true; say('PLUGIN ENGINE · ' + sampleRate + ' HZ · DAW TRANSPORT DRIVES THE SEQUENCER'); }
    ui.idleDrawn = false;
    leds();
    return true;
  };
  updateAudio = function () { schedulePush(); };
  applyMatrix = function () { schedulePush(); };
  applyLFO = function () { schedulePush(); };
  applyTrem = function () {};
  trigger = function (midi = 48, at = null, duration = 0.28, only = null, velocity = 1) {
    pushNow();
    emit('rackNote', { midi, duration, velocity, bank: only ? only[0] : -1, slot: only && only.length > 1 ? only[1] : -1, force: false });
  };
  voice = function (type, midi, start, duration, destination, velocity = 1) {
    pushNow();
    if (destination && typeof destination.bank === 'number')
      emit('rackNote', { midi, duration, velocity, bank: destination.bank, slot: destination.slot, force: true });
    return { sources: [], nodes: [], env: { disconnect() {} }, tone: { disconnect() {} }, end: 0 };
  };
  stopDrone = function () { droneRecords = []; emit('rackStopNotes', {}); };
  seqTick = function () {};
  toggleSeq = async function () {
    await ensure();
    pushNow();
    setRun(!running);
    say(running ? 'SEQUENCER RUNNING · ' + state.tempo + ' BPM · DAW PLAY ALSO RUNS IT' : 'SEQUENCER STOPPED');
  };
  stopSeq = function () {
    if (running) setRun(false);
    ui.playCells.forEach(c => c && c.classList.remove('play'));
    ui.playCells = [];
  };
  panic = function () { emit('rackPanic', {}); say('ALL VOICES STOPPED'); };
  killAudio = function () { stopSeq(); emit('rackPanic', {}); say('VOICES STOPPED · THE ENGINE RUNS IN THE PLUGIN'); };
  download = async function (obj, name) {
    emit('rackSave', { name, json: JSON.stringify(obj, null, 2) });
    say('SAVE ' + String(name).toUpperCase() + ' · CHOOSE A FOLDER');
  };
  midiConnect = async function () {
    const d = $('midiDev'); if (d) d.textContent = 'HOST MIDI';
    say("MIDI COMES FROM THIS PLUGIN'S TRACK IN THE DAW");
  };

  // ── plugin → page ──────────────────────────────────────────────────────────────
  backend.addEventListener('rackLoad', d => {
    applying = true;
    try {
      ui.playCells.forEach(c => c && c.classList.remove('play'));
      ui.playCells = [];
      state = JSON.parse(d.stateJson);
      normalize();
      if (typeof d.sceneIndex === 'number' && d.sceneIndex >= 0 && d.sceneIndex < SCENES.length) { ui.scene = d.sceneIndex; ui.scenePick = d.sceneIndex; }
      ui.editStep = 0; ui.lfoKey = null; ui.tremKey = null;
      buildRack();
      lastSent = JSON.stringify(state);
      if (d.name) say('LOADED ' + String(d.name).toUpperCase());
    } catch (err) {
      console.error('continuum bridge: load failed', err);
      say('PLUGIN PATCH COULD NOT BE SHOWN · ' + err.message);
    } finally {
      applying = false;
    }
  });

  const MKEYS = ['master', 'drive', 'delayMix', 'delayTime', 'feedback', 'lfoRate', 'lfoDepth', 'sendLevel', 'tempo', 'root'];
  backend.addEventListener('rackParams', d => {
    pushNow();   // send any local edit first, so it is not mistaken for the plugin's values
    let touched = false;
    for (const k of MKEYS) if (k in d && state[k] !== d[k]) { state[k] = d[k]; syncKey('m:' + k); touched = true; }
    for (const k of ['lfoDest', 'lfoWave']) if (k in d && state[k] !== d[k]) { state[k] = d[k]; syncSelKey('ms:' + k); touched = true; }
    (d.banks || []).forEach((bv, b) => {
      for (const k of ['level', 'pan', 'cutoff']) if (k in bv && state.banks[b][k] !== bv[k]) { state.banks[b][k] = bv[k]; syncKey('b:' + b + ':' + k); touched = true; }
      if ('mute' in bv && state.bankPatterns[b].mute !== bv.mute) {
        state.bankPatterns[b].mute = bv.mute;
        rack.querySelectorAll(`[data-act="mute"][data-arg="${b}"] .led`).forEach(l => l.classList.toggle('on', bv.mute));
        touched = true;
      }
    });
    if (touched) { info(); updateLfoInfo(); lastSent = JSON.stringify(state); }
  });

  backend.addEventListener('rackStatus', d => {
    if (d.sr) sampleRate = d.sr;
    if (!!d.run !== running) { running = !!d.run; seqTimer = running ? 1 : null; leds(); }
    if (running && d.tick !== lastTick && typeof d.tick === 'number') { lastTick = d.tick; setPlayhead(d.tick); }
    if (!running && ui.playCells.length) { ui.playCells.forEach(c => c && c.classList.remove('play')); ui.playCells = []; lastTick = null; }
    if (Array.isArray(d.hits)) {
      const now = performance.now();
      d.hits.forEach((h, i) => { if (lastHits[i] !== null && h !== lastHits[i]) ui.hits[i >> 3][i & 7] = now; lastHits[i] = h; });
    }
    if (Array.isArray(d.banks) && bankAnalysers.length === 8) d.banks.forEach((v, b) => bankAnalysers[b].data.fill(v));
    if (Array.isArray(d.master) && masterAnalyser) resampleInto(masterAnalyser.data, d.master);
    if (Array.isArray(d.octo) && d.octoBank >= 0 && slotAnalysers[d.octoBank]) d.octo.forEach((w, s) => resampleInto(slotAnalysers[d.octoBank][s].data, w));
    if (typeof d.midiNote === 'number' && d.midiNote >= 0) { ui.midiHit = performance.now(); const el = $('midiNote'); if (el) el.textContent = noteName(d.midiNote); }
    if (ui.octoBank !== scopeBank) { scopeBank = ui.octoBank; emit('rackScope', { bank: scopeBank }); }
  });

  // buildRack() redraws the modules, so re-label MIDI IN after every rebuild
  const pageBuildRack = buildRack;
  buildRack = function () {
    pageBuildRack();
    const d = $('midiDev'); if (d) d.textContent = 'HOST MIDI';
  };

  // ── start ──────────────────────────────────────────────────────────────────────
  makeAudio();
  ensure();
  midiConnect();
  const dev = $('midiDev'); if (dev) dev.textContent = 'HOST MIDI';
  emit('rackHello', { version: 1 });
})();
