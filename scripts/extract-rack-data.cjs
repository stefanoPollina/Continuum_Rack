#!/usr/bin/env node
/* Extracts the factory scenes and Mosaic wavetables from the Continuum Rack web app
   (stefanoPollina/Hybrid index.html) into the plugin's Resources folder, along with a
   compressed copy of the page itself for the plugin's web rack view.

   usage: node scripts/extract-rack-data.cjs path/to/Hybrid/index.html [outDir]

   The web app's own scripts are evaluated in a sandbox with no DOM, up to the start of
   its audio engine, so the scene data comes out exactly as the page's applyScene() and
   normalize() produce it, and the wavetable coefficients exactly as its HWT builder
   hands them to createPeriodicWave(). */
'use strict';
const fs = require('fs'), path = require('path'), vm = require('vm'), zlib = require('zlib'), crypto = require('crypto');

const src = process.argv[2];
const outDir = process.argv[3] || path.join(__dirname, '..', 'Resources');
if (!src) { console.error('usage: extract-rack-data.cjs <Hybrid/index.html> [outDir]'); process.exit(2); }
const html = fs.readFileSync(src, 'utf8');
const sha = crypto.createHash('sha256').update(html).digest('hex');

// Locate the three <script> blocks by content rather than line number.
const blocks = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]);
const wtCore = blocks.find(b => b.includes('Mosaic WT-Core'));
const wtBank = blocks.find(b => b.startsWith('window.MOSAIC_WT_BANK'));
const app = blocks.find(b => b.includes('const SCENES='));
if (!wtCore || !wtBank || !app) throw new Error('could not find the expected script blocks in ' + src);
const cut = app.indexOf('/* ═════════════ audio engine');
if (cut < 0) throw new Error('audio engine marker not found');
const appData = app.slice(0, cut);

const noop = new Proxy(function () {}, { get: (t, k) => k === Symbol.toPrimitive ? () => '' : noop, apply: () => noop, construct: () => noop });
const sb = { console, performance: { now: () => 0 }, document: noop, navigator: {},
  localStorage: { getItem: () => null, setItem() {} }, setTimeout() {}, setInterval() {}, requestAnimationFrame() {}, addEventListener() {} };
sb.window = sb; sb.self = sb;
vm.createContext(sb);
vm.runInContext(wtCore + '\n' + wtBank, sb, { filename: 'wt' });
vm.runInContext(appData + '\n;globalThis.__x={SCENES,applyScene,getState:()=>state,FACT_ENG};', sb, { filename: 'app' });
const { SCENES, applyScene, getState, FACT_ENG } = sb.__x;

// ---- scenes -------------------------------------------------------------------------
const scenes = SCENES.map(sc => {
  applyScene(sc);
  const st = JSON.parse(JSON.stringify(getState()));
  return { name: sc.name, tag: sc.tag || '', desc: sc.desc || '', state: st };
});
const usedTables = new Set();
for (const s of scenes) for (const b of s.state.banks) for (const sl of b.slots) if (sl.edit.wtTable) usedTables.add(sl.edit.wtTable);

// ---- wavetables (same steps as the page's HWT.build) ---------------------------------
const WT = sb.WT, bankJson = sb.MOSAIC_WT_BANK, B = WT.makeBank(bankJson);
const H = 512;
const chunks = [];
const u32 = v => { const b = Buffer.alloc(4); b.writeUInt32LE(v); return b; };
const f32 = v => { const b = Buffer.alloc(4); b.writeFloatLE(v); return b; };
const str = s => { const b = Buffer.from(s, 'utf8'); return Buffer.concat([u32(b.length), b]); };
chunks.push(Buffer.from('CRWT'), u32(1), u32(bankJson.tables.length), u32(H));
let maxErr = 0;
for (const t of bankJson.tables) {
  const tab = WT.buildTable(t, { frames: 17, bank: B, resolve: x => B.byId[x] });
  chunks.push(str(t.id), str(t.name), str(t.bank || ''), u32(tab.frames.length));
  for (const fr of tab.frames) {
    const re = Float64Array.from(fr), im = new Float64Array(fr.length);
    WT.fft(re, im, false);
    const n = Math.min(H, fr.length / 2 - 1), real = new Float64Array(H + 1), imag = new Float64Array(H + 1);
    for (let k = 1; k <= n; k++) { real[k] = re[k]; imag[k] = -im[k]; }
    let peak = 0; for (let k = 1; k <= H; k++) peak = Math.max(peak, Math.abs(real[k]), Math.abs(imag[k]));
    const scale = peak > 0 ? peak / 32767 : 1;
    const q = Buffer.alloc(H * 4);
    for (let k = 1; k <= H; k++) {
      const a = Math.round(real[k] / scale), b = Math.round(imag[k] / scale);
      q.writeInt16LE(a, (k - 1) * 4); q.writeInt16LE(b, (k - 1) * 4 + 2);
      if (peak > 0) maxErr = Math.max(maxErr, Math.abs(a * scale - real[k]) / peak, Math.abs(b * scale - imag[k]) / peak);
    }
    chunks.push(f32(scale), q);
  }
}
fs.mkdirSync(outDir, { recursive: true });
const wtBin = zlib.gzipSync(Buffer.concat(chunks), { level: 9 });
fs.writeFileSync(path.join(outDir, 'wavetables.bin.gz'), wtBin);

const scenesOut = { format: 'continuum-rack-scenes-1', source: { file: 'Hybrid/index.html', sha256: sha }, scenes };
fs.writeFileSync(path.join(outDir, 'scenes.json.gz'), zlib.gzipSync(Buffer.from(JSON.stringify(scenesOut)), { level: 9 }));

const enginePatches = FACT_ENG.map(p => ({ name: p.name, group: p.group, type: p.type, edit: p.edit }));
fs.writeFileSync(path.join(outDir, 'engine-patches.json'), JSON.stringify({ format: 'continuum-rack-engine-patches-1', patches: enginePatches }, null, 1));

// The page itself, for the plugin's web rack view (the bridge script is added at run time).
fs.mkdirSync(path.join(outDir, 'web'), { recursive: true });
fs.writeFileSync(path.join(outDir, 'web', 'index.html.gz'), zlib.gzipSync(Buffer.from(html, 'utf8'), { level: 9 }));

const meta = { source: 'stefanoPollina/Hybrid index.html', sha256: sha, scenes: scenes.length, tables: bankJson.tables.length,
  framesPerTable: 17, harmonics: H, tablesUsedByScenes: [...usedTables].sort(), enginePatches: enginePatches.length,
  quantisation: 'int16 per frame, peak-relative', maxRelativeQuantisationError: maxErr };
fs.writeFileSync(path.join(outDir, 'resource-metadata.json'), JSON.stringify(meta, null, 1) + '\n');
console.log(`scenes ${scenes.length}, tables ${bankJson.tables.length}, engine patches ${enginePatches.length}`);
console.log(`wavetables.bin.gz ${(wtBin.length / 1024).toFixed(0)} KB, max quantisation error ${maxErr.toExponential(2)}`);
console.log('tables used by scenes:', [...usedTables].join(' ') || '(none)');
