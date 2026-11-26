// Portable tests for the JUCE-independent rack engine.
// Optional argument: path to an uncompressed wavetables.bin for the Mosaic table checks.

#include "AllocationWatch.h"
#include "RackEngine.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__SSE__) || defined(_M_X64)
 #include <xmmintrin.h>
#endif
#if RACK_HAVE_ZLIB
 #include <zlib.h>
#endif

using namespace crack;

namespace
{
int failures = 0, passes = 0;
void check (bool ok, const std::string& what)
{
    if (ok) ++passes;
    else { ++failures; std::cerr << "FAIL: " << what << "\n"; }
}

struct Rig
{
    WaveLibrary& lib;
    RackEngine engine;
    std::unique_ptr<Program> program;
    LiveParams lp;
    EngineOptions opt;
    Transport tr;
    double sr;
    int block;
    std::vector<float> L, R;
    std::vector<MidiEvent> ev;

    Rig (WaveLibrary& l, const RackState& s, double sampleRate = 48000, int blockSize = 256)
        : lib (l), sr (sampleRate), block (blockSize)
    {
        engine.prepare (sr, block);
        load (s);
        L.resize ((size_t) block); R.resize ((size_t) block);
        ev.reserve (256);
    }
    void load (const RackState& s)
    {
        program = buildProgram (s, lib);
        lp = LiveParams::fromState (program->state);
        engine.setProgram (program.get());
    }
    // Renders seconds of audio; returns {peak, rms}. Calls perBlock before each block.
    std::pair<double, double> run (double seconds, const std::function<void (int64_t, std::vector<MidiEvent>&)>& perBlock = {})
    {
        const int64_t blocks = (int64_t) std::ceil (seconds * sr / block);
        double pk = 0, sum = 0; int64_t count = 0;
        for (int64_t b = 0; b < blocks; ++b)
        {
            ev.clear();
            if (perBlock) perBlock (b, ev);
            engine.process (L.data(), R.data(), block, lp, opt, tr, ev.data(), (int) ev.size());
            if (tr.playing && tr.hasPosition) tr.ppq += block * tr.bpm / 60.0 / sr;
            for (int i = 0; i < block; ++i)
            {
                const double a = L[(size_t) i], c = R[(size_t) i];
                if (! std::isfinite (a) || ! std::isfinite (c)) return { NAN, NAN };
                pk = std::max (pk, std::max (std::abs (a), std::abs (c)));
                sum += a * a + c * c; count += 2;
            }
        }
        return { pk, std::sqrt (sum / std::max<int64_t> (1, count)) };
    }
    uint32_t hits (int b, int s) const { return engine.slotHits[(size_t) b][(size_t) s].load(); }
};

RackState silentRack()
{
    auto s = makeDefaultRack();
    for (auto& b : s.banks) for (auto& sl : b.slots) sl.on = false;
    for (auto& p : s.patterns) for (auto& st : p.steps) st.on = false;
    return s;
}

void testTables (WaveLibrary& lib)
{
    for (int w = 0; w < 4; ++w)
    {
        const auto* t = lib.basic ((Wave) w);
        const auto& lv = t->levelFor (30, 48000);
        float pk = 0;
        for (float v : lv) pk = std::max (pk, std::abs (v));
        check (pk > 0.9f && pk < 1.02f, "basic wave " + std::to_string (w) + " is normalised (peak " + std::to_string (pk) + ")");
    }
    // A 5 kHz saw at 48 kHz may only carry harmonics 1..4.
    const auto& hi = lib.basic (Wave::sawtooth)->levelFor (5000, 48000);
    double worst = 0;
    const int N = (int) hi.size() - 1;
    for (int k = 5; k < N / 2; ++k)
    {
        double re = 0, im = 0;
        for (int i = 0; i < N; ++i) { re += hi[(size_t) i] * std::cos (6.283185307 * k * i / N); im += hi[(size_t) i] * std::sin (6.283185307 * k * i / N); }
        worst = std::max (worst, std::hypot (re, im) / N);
    }
    check (worst < 1e-4, "mip level for 5 kHz holds nothing above Nyquist");

    EngineEdit e;
    const auto* p1 = lib.partials (e);
    const auto* p2 = lib.partials (e);
    check (p1 == p2, "identical partial spectra share one table");
}

void testOneShot (WaveLibrary& lib)
{
    auto s = silentRack();
    s.banks[0].slots[0].on = true;
    s.banks[0].slots[0].type = hybrid;
    Rig rig (lib, s);
    rig.opt.midiGate = MidiGate::oneShot;
    auto r = rig.run (0.3, [] (int64_t b, std::vector<MidiEvent>& ev) { if (b == 0) ev.push_back ({ MidiEvent::noteOn, 10, 60, 1.0f }); });
    check (std::isfinite (r.first) && r.first > 1e-3, "one-shot note sounds");
    check (rig.engine.countActiveVoices() == 1, "one-shot voice alive during its tail");
    // 0.42 s note + release (0.01 + 0.25*3) + 0.08 = 1.26 s total
    rig.run (1.1);
    check (rig.engine.countActiveVoices() == 0, "one-shot voice ends after duration + release");
    auto quiet = rig.run (0.5);
    check (quiet.first < 1e-3, "rack falls quiet after the echo dies away (peak " + std::to_string (quiet.first) + ")");
}

void testGated (WaveLibrary& lib)
{
    auto s = silentRack();
    s.banks[2].slots[3].on = true;
    s.banks[2].slots[3].edit.sustain = 80;
    Rig rig (lib, s);
    rig.opt.midiGate = MidiGate::gated;
    rig.run (0.05, [] (int64_t b, std::vector<MidiEvent>& ev) { if (b == 0) ev.push_back ({ MidiEvent::noteOn, 0, 48, 0.8f }); });
    auto held = rig.run (2.0);
    check (held.second > 1e-4 && rig.engine.countActiveVoices() == 1, "gated note holds at sustain");
    rig.run (0.01, [] (int64_t b, std::vector<MidiEvent>& ev) { if (b == 0) ev.push_back ({ MidiEvent::noteOff, 5, 48, 0 }); });
    rig.run (0.01 + 0.25 * 3 + 0.08 + 0.05);
    check (rig.engine.countActiveVoices() == 0, "gated note ends after release");

    // Sustain pedal defers the release.
    rig.run (0.05, [] (int64_t b, std::vector<MidiEvent>& ev) {
        if (b == 0) { ev.push_back ({ MidiEvent::sustain, 0, 64, 1 }); ev.push_back ({ MidiEvent::noteOn, 1, 50, 1 }); ev.push_back ({ MidiEvent::noteOff, 100, 50, 0 }); } });
    rig.run (1.5);
    check (rig.engine.countActiveVoices() == 1, "sustain pedal holds a released note");
    rig.run (0.01, [] (int64_t b, std::vector<MidiEvent>& ev) { if (b == 0) ev.push_back ({ MidiEvent::sustain, 0, 64, 0 }); });
    rig.run (1.0);
    check (rig.engine.countActiveVoices() == 0, "pedal up releases held notes");
}

void testHostSequencer (WaveLibrary& lib)
{
    auto s = makeDefaultRack();   // bank 1 slot 1, steps 1,5,9,13: quarter notes
    Rig rig (lib, s, 48000, 128);
    rig.tr = { true, true, 0.0, 120.0 };
    rig.opt.seqMode = SeqMode::host;
    rig.run (2.0);                // one bar at 120 BPM
    check (rig.hits (0, 0) == 4, "host sync: four quarter-note steps per bar (got " + std::to_string (rig.hits (0, 0)) + ")");

    // Stopped transport plays nothing.
    rig.tr.playing = false;
    const auto before = rig.hits (0, 0);
    rig.run (1.0);
    check (rig.hits (0, 0) == before, "stopped host transport triggers no steps");

    // Loop wrap: jumping back to 0 retriggers step 1.
    rig.tr = { true, true, 3.9, 120.0 };
    const auto h0 = rig.hits (0, 0);
    rig.run (0.01);
    rig.tr.ppq = 0.0;
    rig.run (0.01);
    check (rig.hits (0, 0) == h0 + 1, "host loop wrap retriggers the first step");

    // Step timing: a step at ppq 1.0 falls on the expected sample.
    auto t = silentRack();
    t.banks[0].slots[0].on = true;
    t.banks[0].slots[0].edit.attack = 0;
    t.patterns[0].steps[4].on = true;
    t.delayMix = 0;
    Rig r2 (lib, t, 48000, 64);
    r2.tr = { true, true, 0.75, 120.0 };   // the step at ppq 1.0 is 0.125 s = 6000 samples away
    r2.lp.delayMix = 0;
    int64_t first = -1, sample = 0;
    for (int b = 0; b < 200 && first < 0; ++b)
    {
        r2.engine.process (r2.L.data(), r2.R.data(), 64, r2.lp, r2.opt, r2.tr, nullptr, 0);
        r2.tr.ppq += 64 * 2.0 / 48000;
        for (int i = 0; i < 64; ++i, ++sample)
            if (first < 0 && std::abs (r2.L[(size_t) i]) > 1e-6) first = sample;
    }
    check (first >= 6000 + 288 && first <= 6010 + 288, "host step lands on its sample plus 6 ms look-ahead (first sound at " + std::to_string (first) + ")");
}

void testInternalClock (WaveLibrary& lib)
{
    auto s = makeDefaultRack();
    s.patterns[0].steps.fill (Step {});
    for (int i = 0; i < 16; ++i) s.patterns[0].steps[(size_t) i].on = true;
    Rig rig (lib, s, 44100, 512);
    rig.opt.seqMode = SeqMode::internal;
    rig.opt.internalRun = true;
    rig.lp.tempo = 150;
    rig.run (137 * 512 / 44100.0);   // 137 blocks: just under one bar (70,560 samples)
    check (rig.hits (0, 0) == 16, "internal clock: 16 steps per bar at 150 BPM (got " + std::to_string (rig.hits (0, 0)) + ")");
    rig.opt.internalRun = false;
    const auto h = rig.hits (0, 0);
    rig.run (0.5);
    check (rig.hits (0, 0) == h, "internal clock stops with Run off");

    rig.opt.seqMode = SeqMode::host;      // host mode with no host position falls back to Run
    rig.tr.hasPosition = false;
    rig.opt.internalRun = true;
    rig.run (0.2);
    check (rig.hits (0, 0) > h, "host mode without host timing falls back to the internal clock");
}

void testArp (WaveLibrary& lib)
{
    // An UP arp over steps pitched 7, 0, 3 plays 0, 3, 7 in step order.
    auto s = silentRack();
    s.banks[0].slots[0].on = true;
    auto& p = s.patterns[0];
    p.length = 8; p.arp = Arp::up;
    p.steps[0] = { true, 7, 100, 55, 78 };
    p.steps[2] = { true, 0, 100, 55, 78 };
    p.steps[4] = { true, 3, 100, 55, 78 };
    Rig rig (lib, s);
    rig.opt.seqMode = SeqMode::internal; rig.opt.internalRun = true;
    rig.run (0.5);
    check (rig.hits (0, 0) >= 3, "arp steps trigger");
}

void testSendsAndLfo (WaveLibrary& lib)
{
    auto s = silentRack();
    s.banks[0].slots[0].on = true;
    s.banks[0].level = 100;
    s.banks[1].level = 100;
    s.cables = { { "bo:0", "bi:1", "#f3374b" }, { "bo:3", "bi:2", "#f3374b" }, { "lfo", "bm:5", "#00b56e" } };
    normalise (s);
    check (s.cables.size() == 2, "backward send dropped by normalise");
    const auto rt = computeRouting (s);
    check (rt.sends.size() == 1 && rt.stereoInput[1] && ! rt.stereoInput[0], "send routing and stereo input");
    check (rt.lfoTarget[5] && ! rt.lfoTarget[0], "patched LFO reaches only its bank");
    auto open = silentRack();
    check (computeRouting (open).lfoTarget[0] && computeRouting (open).lfoTarget[7], "unpatched LFO reaches all banks");

    Rig rig (lib, s);
    rig.lp.sendLevel = 40;
    rig.opt.midiGate = MidiGate::gated;
    rig.run (0.3, [] (int64_t b, std::vector<MidiEvent>& ev) { if (b == 0) ev.push_back ({ MidiEvent::noteOn, 0, 57, 1 }); });
    check (rig.engine.bankPeak[1].load() > 1e-4, "send carries bank 1 into bank 2");
    check (rig.engine.bankPeak[2].load() == 0, "unrelated bank stays silent");

    // Thirteen sends keep the last twelve.
    auto many = silentRack();
    for (int a = 0; a < 7; ++a) for (int b = a + 1; b < 8 && many.cables.size() < 13; ++b)
        many.cables.push_back ({ "bo:" + std::to_string (a), "bi:" + std::to_string (b), "#3695ef" });
    normalise (many);
    check (many.cables.size() <= 7, "one cable per input (last wins)");
}

void testAllEnginesAndStress (WaveLibrary& lib)
{
    // Every engine type, every filter, plus comb and wavefold extremes.
    for (int type = 0; type < 8; ++type)
        for (int f = 0; f < 5; ++f)
        {
            auto s = silentRack();
            auto& sl = s.banks[(size_t) type].slots[0];
            sl.on = true; sl.type = type;
            sl.edit.filter = (VoiceFilter) f;
            sl.edit.resonance = 100; sl.edit.pd = 100; sl.edit.index = 100; sl.edit.envDepth = 100;
            sl.edit.lfoDepth = 100; sl.edit.lfoTarget = (LfoTarget) (f % 3); sl.edit.combFeedback = 90;
            sl.edit.wtEnabled = (type == wavetable);
            Rig rig (lib, s);
            rig.opt.midiGate = MidiGate::oneShot;
            auto r = rig.run (0.6, [] (int64_t b, std::vector<MidiEvent>& ev) { if (b == 0) for (int n : { 24, 60, 108 }) ev.push_back ({ MidiEvent::noteOn, 3, n, 1 }); });
            check (std::isfinite (r.first) && r.first < 8.0, "engine " + std::to_string (type) + " filter " + std::to_string (f) + " finite and bounded (peak " + std::to_string (r.first) + ")");
        }

    // Full rack: 64 slots, every step on with two-bar gates: saturates the voice pool.
    auto s = makeDefaultRack();
    for (auto& b : s.banks) { b.insert = Insert::drive; for (auto& sl : b.slots) sl.on = true; }
    for (auto& p : s.patterns) { p.length = 64; for (auto& st : p.steps) st = { true, 12, 127, 3200, 100 }; }
    s.cables.clear();
    for (int a = 0; a < 7; ++a) s.cables.push_back ({ "bo:" + std::to_string (a), "bi:" + std::to_string (a + 1), "#3695ef" });
    s.feedback = 70; s.delayMix = 65; s.drive = 100; s.sendLevel = 40;
    Rig rig (lib, s, 96000, 512);
    rig.opt.seqMode = SeqMode::internal; rig.opt.internalRun = true;
    rig.lp.tempo = 240;
    rig.run (0.2);   // warm up past the first allocation-free block
    allocationWatch::count = 0;
    allocationWatch::active = true;
    auto r = rig.run (3.0, [] (int64_t b, std::vector<MidiEvent>& ev) { if (b % 7 == 0) ev.push_back ({ MidiEvent::noteOn, 11, 30 + (int) (b % 60), 1 }); });
    allocationWatch::active = false;
    check (std::isfinite (r.first), "stress render stays finite");
    check (rig.engine.countActiveVoices() == RackEngine::maxVoices, "stress fills the voice pool");
    check (allocationWatch::count.load() == 0, "process() does not allocate (" + std::to_string (allocationWatch::count.load()) + " allocations)");
}

std::vector<char> readGzip (const char* path)
{
    std::vector<char> out;
#if RACK_HAVE_ZLIB
    gzFile f = gzopen (path, "rb");
    if (! f) return out;
    char buf[65536];
    int n;
    while ((n = gzread (f, buf, sizeof buf)) > 0) out.insert (out.end(), buf, buf + n);
    gzclose (f);
#else
    (void) path;
#endif
    return out;
}

void testMosaic (WaveLibrary& lib, const char* path)
{
    const auto data = readGzip (path);
    check (! data.empty() && lib.loadMosaicBank (data.data(), data.size()), "Mosaic bank loads");
    check (lib.mosaicTables().size() == 131, "131 Mosaic tables");
    check (lib.mosaicFrameCount ("WT305") == 17, "17 frames per table");
    const auto* t = lib.mosaic ("WT305", 8);
    check (t != nullptr && t == lib.mosaic ("WT305", 8), "table frame is cached");
    float pk = 0;
    for (float v : t->levelFor (40, 48000)) pk = std::max (pk, std::abs (v));
    check (pk > 0.9f && pk < 1.02f, "Mosaic frame normalised like a PeriodicWave");
    check (! lib.loadMosaicBank (data.data(), data.size() / 2), "truncated bank is rejected");

    auto s = silentRack();
    auto& e = s.banks[0].slots[0].edit;
    s.banks[0].slots[0].on = true; s.banks[0].slots[0].type = wavetable;
    e.wtEnabled = true; e.wtTable = "WT305"; e.wtScan = 10; e.wtSweep = 80; e.attack = 20;
    auto prog = buildProgram (s, lib);
    check (prog->setup[0][0].hops == 4, "scan sweep crosses four frame hops");
    Rig rig (lib, s);
    rig.opt.midiGate = MidiGate::oneShot;
    auto r = rig.run (0.8, [] (int64_t b, std::vector<MidiEvent>& ev) { if (b == 0) ev.push_back ({ MidiEvent::noteOn, 0, 45, 1 }); });
    check (std::isfinite (r.first) && r.first > 1e-3, "wavetable hop voice sounds");
}
} // namespace

int main (int argc, char** argv)
{
#if defined(__SSE__) || defined(_M_X64)
    _mm_setcsr (_mm_getcsr() | 0x8040);   // flush denormals, as the plugin does
#endif
    try
    {
        WaveLibrary lib;
        testTables (lib);
        testOneShot (lib);
        testGated (lib);
        testHostSequencer (lib);
        testInternalClock (lib);
        testArp (lib);
        testSendsAndLfo (lib);
        testAllEnginesAndStress (lib);
#if RACK_HAVE_ZLIB
        if (argc > 1) testMosaic (lib, argv[1]);
        else std::cout << "(Mosaic table checks skipped: pass Resources/wavetables.bin.gz)\n";
#else
        (void) argc; (void) argv;
        std::cout << "(Mosaic table checks skipped: built without zlib; the processor tests cover the bank)\n";
#endif
    }
    catch (const std::exception& e)
    {
        std::cerr << "exception: " << e.what() << "\n";
        return 2;
    }
    std::cout << passes << " checks passed, " << failures << " failed\n";
    return failures ? 1 : 0;
}
