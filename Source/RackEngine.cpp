#include "RackEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace crack
{
namespace
{
constexpr double twoPi = 6.283185307179586476925286766559;
constexpr double halfPi = 1.5707963267948966192313216916398;
constexpr double never = 1e300;
constexpr int renderQuantum = 128;   // Web Audio: a delay inside a cycle is at least one quantum

template <typename T> T clampv (T v, T lo, T hi) { return std::min (hi, std::max (lo, v)); }
inline double frac (double x) { return x - std::floor (x); }

// setTargetAtTime over dt seconds with time constant tau.
inline double approach (double value, double target, double dt, double tau)
{
    return target + (value - target) * std::exp (-dt / std::max (1e-6, tau));
}

// The web app's waveShape(amount) curve, read the way a WaveShaperNode reads it.
struct Shaper
{
    double a = 1, norm = 1;
    void set (double amount) { a = 1 + amount * 35; norm = 1.0 / std::tanh (a); }
    inline float operator() (float x) const noexcept
    {
        x = clampv (x, -1.0f, 1.0f);
        return (float) (std::tanh (a * x) * norm);
    }
};

enum class BqType { lowpass, highpass, bandpass, notch };

// Web Audio BiquadFilterNode (Audio EQ Cookbook; Q in dB for low/high-pass).
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    void set (BqType type, double freq, double Q, double sr)
    {
        const double nyq = 0.5 * sr;
        freq = clampv (freq, 1.0, nyq * 0.9999);
        const double w0 = twoPi * freq / sr, c = std::cos (w0), s = std::sin (w0);
        double alpha, n0, n1, n2;
        switch (type)
        {
            case BqType::lowpass:
                alpha = s / (2 * std::pow (10.0, Q / 20.0));
                n0 = (1 - c) / 2; n1 = 1 - c; n2 = n0; break;
            case BqType::highpass:
                alpha = s / (2 * std::pow (10.0, Q / 20.0));
                n0 = (1 + c) / 2; n1 = -(1 + c); n2 = n0; break;
            case BqType::bandpass:
                alpha = s / (2 * std::max (1e-4, Q));
                n0 = alpha; n1 = 0; n2 = -alpha; break;
            case BqType::notch: default:
                alpha = s / (2 * std::max (1e-4, Q));
                n0 = 1; n1 = -2 * c; n2 = 1; break;
        }
        const double a0 = 1 + alpha;
        b0 = n0 / a0; b1 = n1 / a0; b2 = n2 / a0;
        a1 = -2 * c / a0; a2 = (1 - alpha) / a0;
    }
    inline float process (float xin) noexcept
    {
        const double x = xin;
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return (float) y;
    }
    void clear() { x1 = x2 = y1 = y2 = 0; }
};

inline float lfoShape (Wave w, double ph) noexcept
{
    switch (w)
    {
        case Wave::sine:     return (float) std::sin (twoPi * ph);
        case Wave::square:   return ph < 0.5 ? 1.0f : -1.0f;
        case Wave::sawtooth: return (float) (2 * frac (ph + 0.5) - 1);
        case Wave::triangle: return (float) (1 - 4 * std::abs (frac (ph + 0.25) - 0.5));
    }
    return 0;
}

double bankCutoffHz (Insert ins, float cutoff)
{
    return ins == Insert::lowpass ? clampv (70 * std::pow (160.0, cutoff / 100.0), 100.0, 11000.0)
                                  : clampv (70 * std::pow (250.0, cutoff / 100.0), 100.0, 16000.0);
}
double stepCutoffHz (float f) { return clampv (70 * std::pow (250.0, f / 100.0), 100.0, 16000.0); }

// Chromium's DynamicsCompressor static curve and make-up gain, with a simple envelope.
struct Compressor
{
    double thresholdDb = -14, kneeDb = 12, ratio = 8, attack = 0.003, release = 0.12;
    double linThr = 0, kneeThr = 0, kneeThrDb = 0, yKneeDb = 0, slope = 0, k = 1, makeup = 1;
    double gainDb = 0, atkCoef = 0, relCoef = 0;

    static double toDb (double x) { return 20 * std::log10 (std::max (1e-20, x)); }
    static double fromDb (double d) { return std::pow (10.0, d / 20.0); }
    double kneeCurve (double x, double kk) const { return x < linThr ? x : linThr + (1 - std::exp (-kk * (x - linThr))) / kk; }
    double slopeAt (double x, double kk) const
    {
        if (x < linThr) return 1;
        const double x2 = x * 1.001;
        return (toDb (kneeCurve (x2, kk)) - toDb (kneeCurve (x, kk))) / (toDb (x2) - toDb (x));
    }
    double saturate (double x) const
    {
        if (x < kneeThr) return kneeCurve (x, k);
        return fromDb (yKneeDb + slope * (toDb (x) - kneeThrDb));
    }
    void prepare (double sr)
    {
        linThr = fromDb (thresholdDb);
        slope = 1 / ratio;
        kneeThrDb = thresholdDb + kneeDb;
        kneeThr = fromDb (kneeThrDb);
        double minK = 0.1, maxK = 10000, kk = 5;
        for (int i = 0; i < 15; ++i)
        {
            if (slopeAt (kneeThr, kk) < slope) maxK = kk; else minK = kk;
            kk = std::sqrt (minK * maxK);
        }
        k = kk;
        yKneeDb = toDb (kneeCurve (kneeThr, k));
        makeup = std::pow (1 / saturate (1.0), 0.6);
        atkCoef = std::exp (-1 / (attack * sr));
        relCoef = std::exp (-1 / (release * sr));
        gainDb = 0;
    }
    // Chromium delays the signal 6 ms behind its detector (pre-delay), so the gain is already
    // falling when a transient reaches the output. Same here; the delay is reported as latency.
    std::vector<float> preL, preR;
    int preW = 0, preD = 0;
    void setPreDelay (int samples)
    {
        preD = std::max (0, samples);
        preL.assign ((size_t) preD + 1, 0.0f);
        preR.assign ((size_t) preD + 1, 0.0f);
        preW = 0;
    }
    inline void process (float& l, float& r) noexcept
    {
        const double x = std::max (std::abs ((double) l), std::abs ((double) r));
        const double target = x > 1e-9 ? toDb (saturate (x) / x) : 0.0;
        const double c = target < gainDb ? atkCoef : relCoef;
        gainDb = target + (gainDb - target) * c;
        const double g = fromDb (gainDb) * makeup;
        float dl = l, dr = r;
        if (preD > 0)
        {
            const int N = preD + 1;
            preL[(size_t) preW] = l; preR[(size_t) preW] = r;
            const int rd = (preW + 1) % N;   // the sample written preD samples ago
            dl = preL[(size_t) rd]; dr = preR[(size_t) rd];
            preW = rd;
        }
        l = (float) (dl * g); r = (float) (dr * g);
    }
};

} // namespace

// ───────────────────────────────────────────────────────────── voice
struct RackEngine::Voice
{
    bool active = false;
    int bank = 0, slot = 0, note = -1;
    bool gated = false;
    uint64_t serial = 0;

    // time, in samples since the voice's start (negative before it starts)
    double age = 0, attackS = 0, offS = never, stopS = never, rampEndS = 0, slewS = 1;
    double sr = 48000;

    // envelope
    double env = 0, peak = 0, sus = 0, decK = 0, relK = 1, relLevel = 0, jump = 0, releaseSec = 0;
    bool released = false;

    // oscillators
    const std::vector<float>* t1 = nullptr;
    const std::vector<float>* t2 = nullptr;
    std::array<const std::vector<float>*, 5> hop {};
    int hops = 0;
    double segS = 1, f = 440, f1 = 440, f2 = 440, g1 = 1, g2 = 0;
    double ph1 = 0, ph2 = 0, phM = 0, phR = 0, phA = 0, phL = 0;
    bool fm = false, shaper = false, reson = false, additive = false;
    double fmInc = 0, fmDepth = 0;
    Shaper shape;

    // per-voice LFO
    bool lfo = false;
    LfoTarget lfoTarget = LfoTarget::filter;
    double lfoInc = 0, lfoAmt = 0;

    // filter
    BqType bqType = BqType::lowpass;
    Biquad bq;
    double freq = 1000, destF = 1000, Q = 1, lastF = -1;
    bool envRamp = false;

    // comb
    bool comb = false;
    std::vector<float> combBuf;
    int combW = 0;
    double combD = 1;
    float combFb = 0;

    double releaseRemainingSamples() const { return released ? stopS - age : never; }

    void start (int type, const EngineEdit& e, const SlotSetup& setup, int midi, double duration,
                float velocity, int offset, bool isGated, double sampleRate)
    {
        sr = sampleRate;
        active = true; gated = isGated; released = false;
        age = -offset;
        f = 440.0 * std::pow (2.0, (midi + e.transpose - 69) / 12.0);

        const double dur = isGated ? never : duration;
        const double attack = std::min (std::max (0.002, dur * 0.6), 0.002 + e.attack / 100.0 * 2);
        const double decay = 0.005 + e.decay / 100.0 * 2, sustain = e.sustain / 100.0;
        releaseSec = 0.01 + e.release / 100.0 * 3;
        peak = std::max (0.0002, 0.11 * velocity);
        sus = std::max (0.0001, peak * sustain);
        attackS = attack * sr;
        decK = std::exp (-1.0 / (std::max (0.003, decay / 3) * sr));
        env = 0.0001; jump = 0; relK = 1;
        slewS = 0.001 * sr;
        if (isGated) { offS = never; stopS = never; }
        else
        {
            offS = std::max (0.05, duration) * sr;
            stopS = offS + (releaseSec + 0.08) * sr;
        }

        // filter
        freq = std::min (19000.0, std::max (35.0, 35 * std::pow (500.0, e.cutoff / 100.0)));
        Q = 0.15 + e.resonance / 100.0 * 22;
        switch (e.filter)
        {
            case VoiceFilter::lowpass:  bqType = BqType::lowpass; break;
            case VoiceFilter::highpass: bqType = BqType::highpass; break;
            case VoiceFilter::notch:    bqType = BqType::notch; break;
            case VoiceFilter::bandpass:
            case VoiceFilter::comb:     bqType = BqType::bandpass; break;
        }
        envRamp = e.envDepth != 0;
        destF = envRamp ? std::max (35.0, std::min (19000.0, freq * std::pow (2.0, e.envDepth / 100.0 * 4))) : freq;
        rampEndS = std::max (0.01, attack + decay) * sr;
        bq.clear(); lastF = -1;

        // oscillators
        f1 = std::max (10.0, f);
        f2 = std::max (10.0, f * std::pow (2.0, e.detune / 1200.0));
        g1 = std::max (0.001, 1 - e.mix / 100.0);
        g2 = e.mix / 100.0;
        ph1 = ph2 = phM = phR = phA = phL = 0;
        t1 = &setup.wave1->levelFor (f1, sr);
        t2 = &setup.wave2->levelFor (f2, sr);
        hops = setup.hops;
        for (int k = 0; k <= hops; ++k) hop[(size_t) k] = &setup.hop[(size_t) k]->levelFor (f1, sr);
        segS = std::max (0.02, attack + decay) / std::max (1, hops) * sr;

        fm = type == fmMatrix || type == crossmod || e.index > 0;
        fmInc = f * e.ratio / sr;
        fmDepth = f * e.index / 100.0 * 4;
        shaper = type == wavefold || e.pd > 0;
        shape.set (std::min (0.95, e.pd / 100.0 * 0.6));
        reson = type == resonator;
        additive = type == EngineType::additive;

        lfo = e.lfoDepth > 0;
        lfoTarget = e.lfoTarget;
        lfoInc = e.lfoRate / sr;
        lfoAmt = lfoTarget == LfoTarget::filter ? freq * 0.7 * e.lfoDepth / 100.0
               : lfoTarget == LfoTarget::pitch  ? f * 0.12 * e.lfoDepth / 100.0
                                                : peak * 0.35 * e.lfoDepth / 100.0;

        comb = e.filter == VoiceFilter::comb;
        if (comb)
        {
            combD = std::max (std::min (0.12, 1.0 / std::max (40.0, f)) * sr, (double) renderQuantum);
            combD = std::min (combD, (double) combBuf.size() - 2);
            combFb = (float) std::min (0.88, e.combFeedback / 100.0 * 0.9);
            std::fill (combBuf.begin(), combBuf.end(), 0.0f);
            combW = 0;
        }
    }

    void noteOff (double atAge)
    {
        if (released || offS != never) return;
        offS = std::max (atAge, 0.05 * sr);
        stopS = offS + (releaseSec + 0.08) * sr;
    }

    inline double hopGain (int k, double t) const noexcept
    {
        const double x = t / segS - k;
        if (k == hops && x >= 0) return g1;
        if (k == 0 && x <= 0) return g1;
        return g1 * std::max (0.0, 1 - std::abs (x));
    }

    // Renders into out[0..n). Returns false once finished.
    bool render (float* out, int n) noexcept
    {
        int i = 0;
        if (age < 0)
        {
            i = (int) std::min ((double) n, -age);
            age += i;
            if (i >= n) return true;
        }
        while (i < n)
        {
            const int chunk = std::min (n - i, controlInterval);

            // control rate: filter frequency
            const double lfoVal = lfo ? std::sin (twoPi * phL) : 0.0;
            double fc = freq;
            if (envRamp && age < rampEndS) fc = destF * std::pow (freq / destF, std::max (0.0, age) / rampEndS);
            if (lfo && lfoTarget == LfoTarget::filter) fc += lfoAmt * lfoVal;
            if (std::abs (fc - lastF) > 1e-3 * std::max (1.0, lastF))
            {
                bq.set (bqType, fc, Q, sr);
                lastF = fc;
            }

            for (int j = 0; j < chunk; ++j, ++i)
            {
                if (age >= stopS) { active = false; return false; }
                if (age >= offS && ! released)
                {
                    released = true;
                    jump = env - sus;
                    relLevel = sus;
                    const double len = std::max (1.0, stopS - offS);
                    relK = std::pow (0.0001 / sus, 1.0 / len);
                }

                // envelope
                if (released)
                {
                    relLevel *= relK;
                    const double s = age - offS;
                    env = relLevel + (s < slewS ? jump * (1 - s / slewS) : 0.0);
                }
                else if (age < attackS)
                    env = 0.0001 + (peak - 0.0001) * age / attackS;
                else
                {
                    if (age - 1 < attackS) env = peak;
                    env = sus + (env - sus) * decK;
                }

                double lv = 0;
                if (lfo) { lv = std::sin (twoPi * phL); phL = frac (phL + lfoInc); }

                // oscillators
                double inst = f1;
                if (fm) { inst += fmDepth * std::sin (twoPi * phM); phM = frac (phM + fmInc); }
                if (lfo && lfoTarget == LfoTarget::pitch) inst += lfoAmt * lv;

                double o1;
                if (hops > 0)
                {
                    o1 = 0;
                    for (int k = 0; k <= hops; ++k)
                    {
                        const double g = hopGain (k, std::max (0.0, age));
                        if (g > 0) o1 += g * MipTable::read (*hop[(size_t) k], ph1);
                    }
                }
                else
                    o1 = g1 * MipTable::read (*t1, ph1);
                ph1 = frac (ph1 + inst / sr);

                double mixv = o1 + g2 * MipTable::read (*t2, ph2);
                ph2 = frac (ph2 + f2 / sr);

                if (reson || additive)
                {
                    if (reson) { mixv += 0.25 * std::sin (twoPi * phR); phR = frac (phR + std::max (10.0, 2 * f) / sr); }
                    if (additive)
                    {
                        for (int h = 2; h <= 5; ++h)
                            mixv += 0.15 / (h * h) * std::sin (twoPi * frac (phA * h));
                        phA = frac (phA + f / sr);
                    }
                }

                float x = (float) mixv;
                if (shaper) x = shape (x);
                float y = bq.process (x);

                if (comb)
                {
                    const int N = (int) combBuf.size();
                    double rp = combW - combD;
                    while (rp < 0) rp += N;
                    const int r0 = (int) rp;
                    const float fr = (float) (rp - r0);
                    const float d = combBuf[(size_t) r0] + fr * (combBuf[(size_t) ((r0 + 1) % N)] - combBuf[(size_t) r0]);
                    combBuf[(size_t) combW] = y + combFb * d;
                    combW = (combW + 1) % N;
                    y += 0.38f * d;
                }

                double gain = env;
                if (lfo && lfoTarget == LfoTarget::amplitude) gain += lfoAmt * lv;
                out[i] += (float) (y * gain);
                age += 1;
            }
        }
        return true;
    }
};

// ───────────────────────────────────────────────────────────── bank strip
struct RackEngine::Bus
{
    Biquad fl, fr;
    Shaper shape;
    double fVal = 1000, fTarget = 1000, fTau = 0.025;
    double amp = 0.7, pan = 0;
    std::vector<float> mono, inL, inR, outL, outR;
    struct FilterEvent { int offset; double target; };
    std::array<FilterEvent, 16> events {};
    int numEvents = 0;
    float peak = 0;

    void addEvent (int offset, double target)
    {
        if (numEvents < (int) events.size()) events[(size_t) numEvents++] = { offset, target };
        else events.back() = { offset, target };
    }
};

// ───────────────────────────────────────────────────────────── master section
struct RackEngine::Master
{
    std::vector<float> dl, dr;
    int w = 0;
    double time = 0.25, mix = 0.12, fb = 0.12, gain = 0.55;
    Shaper drive;
    float lastDrive = -1;
    Compressor comp;
};

LiveParams LiveParams::fromState (const RackState& s)
{
    LiveParams p;
    p.master = s.master; p.drive = s.drive; p.delayMix = s.delayMix; p.delayTime = s.delayTime;
    p.feedback = s.feedback; p.lfoRate = s.lfoRate; p.lfoDepth = s.lfoDepth; p.lfoDest = s.lfoDest;
    p.lfoWave = s.lfoWave; p.sendLevel = s.sendLevel; p.root = s.root; p.tempo = s.tempo;
    for (int b = 0; b < numBanks; ++b)
    {
        p.level[(size_t) b] = s.banks[(size_t) b].level;
        p.pan[(size_t) b] = s.banks[(size_t) b].pan;
        p.cutoff[(size_t) b] = s.banks[(size_t) b].cutoff;
        p.seqMute[(size_t) b] = s.patterns[(size_t) b].mute;
    }
    return p;
}

std::unique_ptr<Program> buildProgram (const RackState& state, WaveLibrary& lib)
{
    auto p = std::make_unique<Program>();
    p->state = state;
    normalise (p->state);
    p->routing = computeRouting (p->state);
    for (int b = 0; b < numBanks; ++b)
        for (int s = 0; s < numSlots; ++s)
        {
            const auto& e = p->state.banks[(size_t) b].slots[(size_t) s].edit;
            auto& su = p->setup[(size_t) b][(size_t) s];
            su.wave1 = lib.basic (e.wave1);
            su.wave2 = lib.basic (e.wave2);
            su.hops = 0;
            if (e.wtEnabled && lib.hasMosaic (e.wtTable))
            {
                // Same frame choice as the web app: scan position, then up to 4 hops along the sweep.
                const int n = lib.mosaicFrameCount (e.wtTable);
                const double p0 = clampv (e.wtScan / 100.0, 0.0, 1.0), p1 = clampv (p0 + e.wtSweep / 100.0, 0.0, 1.0);
                const int i0 = (int) std::lround (p0 * (n - 1)), i1 = (int) std::lround (p1 * (n - 1));
                const int hops = std::min (4, std::abs (i1 - i0));
                for (int k = 0; k <= hops; ++k)
                {
                    const int idx = (int) std::lround (i0 + (i1 - i0) * (hops ? (double) k / hops : 0.0));
                    su.hop[(size_t) k] = lib.mosaic (e.wtTable, idx);
                }
                su.wave1 = su.hop[0];
                su.hops = hops;
            }
            else if (e.wtEnabled)
                su.wave1 = lib.partials (e);
            for (int k = su.hops + 1; k < 5; ++k) su.hop[(size_t) k] = su.wave1;
            if (su.hops == 0) su.hop[0] = su.wave1;
        }
    return p;
}

RackEngine::RackEngine()
{
    for (auto& b : playStep) b.store (-1);
}

RackEngine::~RackEngine() = default;

void RackEngine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    voices.assign ((size_t) maxVoices, Voice {});
    const size_t combLen = (size_t) std::ceil (std::max (0.026 * sr, (double) renderQuantum + 4)) + 4;
    for (auto& v : voices) v.combBuf.assign (combLen, 0.0f);

    buses.reset (new Bus[numBanks]);
    for (int b = 0; b < numBanks; ++b)
    {
        auto& bus = buses[(size_t) b];
        for (auto* v : { &bus.mono, &bus.inL, &bus.inR, &bus.outL, &bus.outR }) v->assign ((size_t) maxBlock, 0.0f);
    }
    master = std::make_unique<Master>();
    const size_t dlen = (size_t) std::ceil (1.5 * sr) + 4;
    master->dl.assign (dlen, 0.0f);
    master->dr.assign (dlen, 0.0f);
    master->comp.prepare (sr);
    master->comp.setPreDelay (getLatencySamples());
    for (auto& sc : slotScratch) sc.assign ((size_t) maxBlock, 0.0f);
    lfoBuf.assign ((size_t) maxBlock, 0.0f);
    tremBuf.assign ((size_t) maxBlock, 0.0f);
    reset();
}

void RackEngine::reset()
{
    for (auto& v : voices) v.active = false;
    for (int b = 0; b < numBanks && buses; ++b)
    {
        auto& bus = buses[(size_t) b];
        bus.fl.clear(); bus.fr.clear();
        bus.numEvents = 0;
        if (program)
        {
            const auto& bk = program->state.banks[(size_t) b];
            bus.fVal = bus.fTarget = bankCutoffHz (bk.insert, bk.cutoff);
        }
        lastCutoff[b] = -1;
        playStep[(size_t) b].store (-1);
    }
    if (master)
    {
        std::fill (master->dl.begin(), master->dl.end(), 0.0f);
        std::fill (master->dr.begin(), master->dr.end(), 0.0f);
        master->w = 0;
        master->comp.gainDb = 0;
        master->comp.setPreDelay (getLatencySamples());
    }
    lfoPhase = tremPhase = 0;
    internalTick = 0; internalPhase = 0; internalWasRunning = false;
    sustainDown = false; heldBySustain.fill (false);
    numUiNotes = 0;
}

void RackEngine::panic() noexcept
{
    for (auto& v : voices) v.active = false;
    if (master)
    {
        std::fill (master->dl.begin(), master->dl.end(), 0.0f);
        std::fill (master->dr.begin(), master->dr.end(), 0.0f);
    }
    sustainDown = false; heldBySustain.fill (false);
}

bool RackEngine::queueUiNote (const UiNote& n) noexcept
{
    if (numUiNotes >= (int) uiNotes.size()) return false;
    uiNotes[(size_t) numUiNotes++] = n;
    return true;
}

void RackEngine::triggerLikeWeb (int midi, double duration, int onlyBank, int onlySlot, float velocity) noexcept
{
    // the web app's trigger(): every enabled engine (at most 64), optionally one bank or slot;
    // when that bank or slot is off, its (first) engine plays anyway
    int count = 0;
    for (int b = 0; b < numBanks; ++b)
        for (int s = 0; s < numSlots; ++s)
        {
            if (! program->state.banks[(size_t) b].slots[(size_t) s].on) continue;
            if (onlyBank >= 0 && (onlyBank != b || (onlySlot >= 0 && onlySlot != s))) continue;
            if (count++ >= 64) continue;
            startVoice (b, s, midi, duration, velocity, 0, uiNoteId, false);
        }
    if (count == 0 && onlyBank >= 0 && onlyBank < numBanks)
        startVoice (onlyBank, onlySlot >= 0 ? onlySlot : 0, midi, duration, velocity, 0, uiNoteId, false);
}

void RackEngine::stopUiNotes() noexcept
{
    for (auto& v : voices)
        if (v.active && v.note == uiNoteId && v.offS == never) v.noteOff (v.age);
    for (auto& v : voices)   // drones are long one-shots: cut them to their release
        if (v.active && v.note == uiNoteId && ! v.released && v.age < v.offS) { v.offS = std::max (v.age, 0.0); v.stopS = v.offS + (v.releaseSec + 0.08) * v.sr; }
}

int RackEngine::countActiveVoices() const noexcept
{
    int n = 0;
    for (const auto& v : voices) n += v.active ? 1 : 0;
    return n;
}

void RackEngine::startVoice (int bank, int slot, int midi, double duration, float velocity, int offset, int noteId, bool gated) noexcept
{
    if (! program) return;
    Voice* target = nullptr;
    double best = -1;
    for (auto& v : voices)
    {
        if (! v.active) { target = &v; break; }
        // steal the voice closest to finishing; a held gated voice only as a last resort
        const double remaining = v.released ? v.stopS - v.age : (v.offS != never ? v.stopS - v.age : 1e12 + (double) v.serial);
        if (target == nullptr || remaining < best) { best = remaining; target = &v; }
    }
    const auto& sl = program->state.banks[(size_t) bank].slots[(size_t) slot];
    target->bank = bank; target->slot = slot; target->note = noteId; target->serial = ++voiceSerial;
    target->start (sl.type, sl.edit, program->setup[(size_t) bank][(size_t) slot], midi, duration, velocity, offset, gated, sr);
    slotHits[(size_t) bank][(size_t) slot].fetch_add (1, std::memory_order_relaxed);
}

void RackEngine::releaseNote (int note, int offset) noexcept
{
    for (auto& v : voices)
        if (v.active && v.gated && v.note == note && v.offS == never)
            v.noteOff (v.age + offset);
}

void RackEngine::midiNote (int note, float velocity, int offset, const EngineOptions& opt) noexcept
{
    const bool gated = opt.midiGate == MidiGate::gated;
    const double dur = 0.42;   // the web app's keyboard and MIDI note length
    int count = 0;
    for (int b = 0; b < numBanks; ++b)
    {
        if (opt.midiBank >= 0 && b != opt.midiBank) continue;
        for (int s = 0; s < numSlots; ++s)
        {
            if (! program->state.banks[(size_t) b].slots[(size_t) s].on) continue;
            if (count++ >= 64) break;
            startVoice (b, s, note, dur, velocity, offset, note, gated);
        }
    }
    if (count == 0 && opt.midiBank >= 0)   // the web app plays slot 1 of an empty bank anyway
        startVoice (opt.midiBank, 0, note, dur, velocity, offset, note, gated);
}

void RackEngine::runStep (int64_t tick, int offset, double bpm, const LiveParams& lp) noexcept
{
    const auto& st = program->state;
    const double beat = 60.0 / clampv (bpm, 40.0, 240.0) / 4.0;
    lastTick.store (tick, std::memory_order_relaxed);
    for (int b = 0; b < numBanks; ++b)
    {
        const auto& p = st.patterns[(size_t) b];
        const int len = p.length;
        const int i = (int) (((tick % len) + len) % len);
        playStep[(size_t) b].store (i, std::memory_order_relaxed);
        if (lp.seqMute[(size_t) b]) continue;
        const auto& step = p.steps[(size_t) i];
        if (! step.on) continue;

        float offsetPitch = step.pitch;
        if (p.arp != Arp::off)
        {
            std::array<float, maxSteps> notes {};
            int n = 0;
            for (int k = 0; k < len; ++k)
                if (p.steps[(size_t) k].on) notes[(size_t) n++] = p.steps[(size_t) k].pitch;
            if (n > 0)
            {
                std::sort (notes.begin(), notes.begin() + n);
                const int64_t j = (tick >= 0 ? tick / len : (tick - len + 1) / len) * len + i;
                const int jm = (int) (((j % n) + n) % n);
                switch (p.arp)
                {
                    case Arp::up: offsetPitch = notes[(size_t) jm]; break;
                    case Arp::down: offsetPitch = notes[(size_t) (n - 1 - jm)]; break;
                    case Arp::updown:
                    {
                        std::array<float, maxSteps * 2> path {};
                        int m = 0;
                        for (int k = 0; k < n; ++k) path[(size_t) m++] = notes[(size_t) k];
                        if (n >= 2) for (int k = n - 2; k >= 1; --k) path[(size_t) m++] = notes[(size_t) k];
                        offsetPitch = path[(size_t) (((j % m) + m) % m)];
                        break;
                    }
                    case Arp::random:
                        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                        offsetPitch = notes[(size_t) (rng % (uint32_t) n)];
                        break;
                    case Arp::off: break;
                }
            }
        }

        const double duration = beat * step.gate / 100.0;
        const float velocity = step.velocity / 127.0f;
        const int midi = (int) clampv ((double) lp.root + offsetPitch, 12.0, 119.0);
        int count = 0;
        const auto& seq = st.engineSequence[(size_t) b];
        for (int s = 0; s < numSlots; ++s)
        {
            if (! st.banks[(size_t) b].slots[(size_t) s].on || ! seq.targets[(size_t) s] || ! seq.masks[(size_t) s][(size_t) i] || count++ >= 8)
                continue;
            startVoice (b, s, midi, duration, velocity, offset, -1, false);
        }
        buses[(size_t) b].addEvent (offset, stepCutoffHz (step.filter));
    }
}

void RackEngine::process (float* left, float* right, int n, const LiveParams& lp, const EngineOptions& opt,
                          const Transport& tr, const MidiEvent* events, int numEvents) noexcept
{
    std::fill (left, left + n, 0.0f);
    std::fill (right, right + n, 0.0f);
    if (! program || n <= 0 || ! master) return;
    n = std::min (n, maxBlock);
    const auto& st = program->state;
    const auto& rt = program->routing;
    const double dtBlock = n / sr;

    const bool programChanged = program != lastProgram;
    lastProgram = program;
    for (int b = 0; b < numBanks; ++b)
    {
        auto& bus = buses[(size_t) b];
        bus.numEvents = 0;
        if (programChanged && lastCutoff[b] >= 0)
            lastCutoff[b] = -2;   // insert may have changed: re-aim the strip filter smoothly
        std::fill (bus.mono.begin(), bus.mono.begin() + n, 0.0f);
        std::fill (bus.inL.begin(), bus.inL.begin() + n, 0.0f);
        std::fill (bus.inR.begin(), bus.inR.begin() + n, 0.0f);
        if (lp.cutoff[(size_t) b] != lastCutoff[b])
        {
            bus.fTarget = bankCutoffHz (st.banks[(size_t) b].insert, lp.cutoff[(size_t) b]);
            bus.fTau = 0.025;
            if (lastCutoff[b] == -1) bus.fVal = bus.fTarget;
            lastCutoff[b] = lp.cutoff[(size_t) b];
        }
    }

    // MIDI
    for (int e = 0; e < numEvents; ++e)
    {
        const auto& ev = events[e];
        const int off = clampv (ev.offset, 0, n - 1);
        switch (ev.type)
        {
            case MidiEvent::noteOn:
                if (ev.note >= 0 && ev.note < 128)
                {
                    heldBySustain[(size_t) ev.note] = false;
                    lastMidiNote.store (ev.note, std::memory_order_relaxed);
                    midiNoteCount.fetch_add (1, std::memory_order_relaxed);
                    if (opt.midiGate == MidiGate::gated) releaseNote (ev.note, off);   // retrigger
                    midiNote (ev.note, ev.velocity, off, opt);
                }
                break;
            case MidiEvent::noteOff:
                if (ev.note >= 0 && ev.note < 128)
                {
                    if (sustainDown) heldBySustain[(size_t) ev.note] = true;
                    else releaseNote (ev.note, off);
                }
                break;
            case MidiEvent::sustain:
                sustainDown = ev.velocity >= 0.5f;
                if (! sustainDown)
                    for (int k = 0; k < 128; ++k)
                        if (heldBySustain[(size_t) k]) { heldBySustain[(size_t) k] = false; releaseNote (k, off); }
                break;
            case MidiEvent::allNotesOff:
                sustainDown = false; heldBySustain.fill (false);
                for (int k = 0; k < 128; ++k) releaseNote (k, off);
                break;
        }
    }

    // Sequencer clock: the host's song position while the host plays; otherwise the internal
    // clock while Run is on (so RUN SEQ also works with the host stopped).
    const bool hostValid = tr.hasPosition && tr.bpm > 0 && std::isfinite (tr.bpm) && std::isfinite (tr.ppq) && std::abs (tr.ppq) < 1e12;
    const bool hostClock = opt.seqMode == SeqMode::host && hostValid && tr.playing;
    usingHostClock.store (opt.seqMode == SeqMode::host && hostValid, std::memory_order_relaxed);
    if (hostClock)
    {
        internalWasRunning = false;
        const double ppqPerSample = tr.bpm / 60.0 / sr;
        const double startSix = tr.ppq * 4, endSix = (tr.ppq + n * ppqPerSample) * 4;
        // pre-roll (negative positions) plays nothing; start at the first 16th at or after zero
        for (int64_t k = std::max<int64_t> (0, (int64_t) std::ceil (startSix - 1e-9)); (double) k < endSix - 1e-9; ++k)
        {
            const int off = clampv ((int) std::lround ((k - startSix) / 4.0 / ppqPerSample), 0, n - 1);
            runStep (k, off, tr.bpm, lp);
        }
        seqRunning.store (true, std::memory_order_relaxed);
    }
    else if (opt.seqMode != SeqMode::off && opt.internalRun)
    {
        const double bpm = clampv ((double) lp.tempo, 40.0, 240.0);
        const double stepSamples = 60.0 / bpm / 4.0 * sr;
        if (! internalWasRunning) { internalTick = 0; internalPhase = 0; internalWasRunning = true; }
        double pos = internalPhase;   // samples until the next step, from block start
        while (pos < n)
        {
            runStep (internalTick++, (int) pos, bpm, lp);
            pos += stepSamples;
        }
        internalPhase = pos - n;
        seqRunning.store (true, std::memory_order_relaxed);
    }
    else
    {
        internalWasRunning = false;
        seqRunning.store (false, std::memory_order_relaxed);
        for (auto& p : playStep) p.store (-1, std::memory_order_relaxed);
    }

    // Notes from the editor (keys, audition, PLAY BANK, factory drone)
    for (int u = 0; u < numUiNotes; ++u)
    {
        const auto& un = uiNotes[(size_t) u];
        if (un.bank >= 0 && un.slot >= 0 && un.force)
            startVoice (un.bank, un.slot, un.midi, un.duration, un.velocity, 0, uiNoteId, false);
        else
            triggerLikeWeb (un.midi, un.duration, un.bank, un.slot, un.velocity);
    }
    numUiNotes = 0;

    // Voices. Voices of the bank shown on the editor's octoscope also go through a per-slot
    // scratch buffer so their waveforms can be displayed.
    const int sb = scopeBank.load (std::memory_order_relaxed);
    for (auto& sc : slotScratch) std::fill (sc.begin(), sc.begin() + n, 0.0f);
    int active = 0;
    for (auto& v : voices)
    {
        if (! v.active) continue;
        const bool scoped = v.bank == sb;
        if (v.render (scoped ? slotScratch[(size_t) v.slot].data() : buses[(size_t) v.bank].mono.data(), n)) ++active;
    }
    if (sb >= 0 && sb < numBanks)
    {
        auto& mono = buses[(size_t) sb].mono;
        for (int s = 0; s < numSlots; ++s)
        {
            const auto& sc = slotScratch[(size_t) s];
            for (int i = 0; i < n; ++i) mono[(size_t) i] += sc[(size_t) i];
            scopes.writeSlot (s, sc.data(), n);
        }
    }
    activeVoices.store (active, std::memory_order_relaxed);

    // Global LFO and tremolo
    const double depth = lp.lfoDepth / 100.0;
    const double lfoTarget = lp.lfoDest == LfoDest::filter ? depth * 2200 : lp.lfoDest == LfoDest::pan ? depth * 0.9 : depth * 0.4;
    const double lfoInc = lp.lfoRate / sr, tremInc = 5.5 / sr;
    for (int i = 0; i < n; ++i)
    {
        lfoBuf[(size_t) i] = lfoShape (lp.lfoWave, lfoPhase);
        tremBuf[(size_t) i] = (float) (0.55 + 0.45 * std::sin (twoPi * tremPhase));
        lfoPhase = frac (lfoPhase + lfoInc);
        tremPhase = frac (tremPhase + tremInc);
    }
    const double lfoGainStart = lfoGainValue;
    lfoGainValue = (float) approach (lfoGainValue, lfoTarget, dtBlock, 0.02);

    // Banks, in order, so sends always flow forward within the block
    const double sendGain = lp.sendLevel / 100.0;
    for (int b = 0; b < numBanks; ++b)
    {
        auto& bus = buses[(size_t) b];
        const auto& bk = st.banks[(size_t) b];
        const bool stereo = rt.stereoInput[(size_t) b];
        const bool lfoHere = rt.lfoTarget[(size_t) b];
        const BqType ft = bk.insert == Insert::highpass ? BqType::highpass : BqType::lowpass;
        const double q = bk.insert == Insert::lowpass ? 1.8 : 0.65;
        bus.shape.set (bk.insert == Insert::drive ? 0.23 : 0.0);
        const bool trem = bk.insert == Insert::ring;
        const double ampTarget = lp.level[(size_t) b] / 100.0, panTarget = (lp.pan[(size_t) b] - 50) / 50.0;
        int ev = 0;
        float peak = 0;

        for (int i = 0; i < n; i += controlInterval)
        {
            const int chunk = std::min (controlInterval, n - i);
            while (ev < bus.numEvents && bus.events[(size_t) ev].offset < i + chunk)
            {
                bus.fTarget = bus.events[(size_t) ev].target;
                bus.fTau = 0.009;
                ++ev;
            }
            const double dt = chunk / sr;
            bus.fVal = approach (bus.fVal, bus.fTarget, dt, bus.fTau);
            bus.amp = approach (bus.amp, ampTarget, dt, 0.015);
            bus.pan = approach (bus.pan, panTarget, dt, 0.015);
            const double lg = lfoGainStart + (lfoGainValue - lfoGainStart) * ((double) i / n);
            const double lv = lfoHere ? lfoBuf[(size_t) i] * lg : 0.0;

            double fc = bus.fVal, amp = bus.amp, pan = bus.pan;
            if (lfoHere)
            {
                if (lp.lfoDest == LfoDest::filter) fc += lv;
                else if (lp.lfoDest == LfoDest::amp) amp += lv;
                else pan += lv;
            }
            bus.fl.set (ft, fc, q, sr);
            bus.fr.b0 = bus.fl.b0; bus.fr.b1 = bus.fl.b1; bus.fr.b2 = bus.fl.b2; bus.fr.a1 = bus.fl.a1; bus.fr.a2 = bus.fl.a2;
            pan = clampv (pan, -1.0, 1.0);

            for (int j = i; j < i + chunk; ++j)
            {
                const float tg = trem ? tremBuf[(size_t) j] : 1.0f;
                const float g = (float) amp * tg;
                float l, r;
                if (! stereo)
                {
                    float x = bus.shape (bus.fl.process (bus.mono[(size_t) j])) * g;
                    const double xn = (pan + 1) * 0.5;
                    l = (float) (x * std::cos (xn * halfPi));
                    r = (float) (x * std::sin (xn * halfPi));
                }
                else
                {
                    const float xl = bus.shape (bus.fl.process (bus.mono[(size_t) j] + bus.inL[(size_t) j])) * g;
                    const float xr = bus.shape (bus.fr.process (bus.mono[(size_t) j] + bus.inR[(size_t) j])) * g;
                    if (pan <= 0)
                    {
                        const double xn = pan + 1;
                        l = (float) (xl + xr * std::cos (xn * halfPi));
                        r = (float) (xr * std::sin (xn * halfPi));
                    }
                    else
                    {
                        l = (float) (xl * std::cos (pan * halfPi));
                        r = (float) (xr + xl * std::sin (pan * halfPi));
                    }
                }
                l *= 0.42f; r *= 0.42f;
                bus.outL[(size_t) j] = l; bus.outR[(size_t) j] = r;
                left[j] += l; right[j] += r;
                peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
            }
        }
        if (! std::isfinite (peak)) peak = 0;
        bankPeak[(size_t) b].store (std::max (peak, bankPeak[(size_t) b].load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);

        for (const auto& s : rt.sends)
        {
            if (s.from != b) continue;
            auto& dst = buses[(size_t) s.to];
            for (int j = 0; j < n; ++j)
            {
                dst.inL[(size_t) j] += (float) (bus.outL[(size_t) j] * sendGain);
                dst.inR[(size_t) j] += (float) (bus.outR[(size_t) j] * sendGain);
            }
        }
    }

    // Master: drive shaper || echo -> compressor -> level
    auto& m = *master;
    if (lp.drive != m.lastDrive) { m.drive.set (lp.drive / 100.0); m.lastDrive = lp.drive; }
    const int N = (int) m.dl.size();
    float peak = 0;
    for (int i = 0; i < n; i += controlInterval)
    {
        const int chunk = std::min (controlInterval, n - i);
        const double dt = chunk / sr;
        m.time = approach (m.time, lp.delayTime, dt, 0.02);
        m.mix = approach (m.mix, lp.delayMix / 100.0, dt, 0.02);
        m.fb = approach (m.fb, lp.feedback / 100.0, dt, 0.02);
        m.gain = approach (m.gain, lp.master / 100.0, dt, 0.012);
        const double D = clampv (m.time * sr, (double) renderQuantum, (double) N - 2);
        for (int j = i; j < i + chunk; ++j)
        {
            double rp = m.w - D;
            if (rp < 0) rp += N;
            const int r0 = (int) rp, r1 = (r0 + 1) % N;
            const float fr = (float) (rp - r0);
            const float dl = m.dl[(size_t) r0] + fr * (m.dl[(size_t) r1] - m.dl[(size_t) r0]);
            const float dr = m.dr[(size_t) r0] + fr * (m.dr[(size_t) r1] - m.dr[(size_t) r0]);
            const float inL = left[j], inR = right[j];
            m.dl[(size_t) m.w] = inL + (float) m.fb * dl;
            m.dr[(size_t) m.w] = inR + (float) m.fb * dr;
            m.w = (m.w + 1) % N;

            float l = m.drive (inL) + (float) m.mix * dl;
            float r = m.drive (inR) + (float) m.mix * dr;
            m.comp.process (l, r);
            l *= (float) m.gain; r *= (float) m.gain;
            left[j] = l; right[j] = r;
            peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
        }
    }
    scopes.writeMaster (left, right, n);
    if (! std::isfinite (peak)) peak = 0;
    masterPeak.store (std::max (peak, masterPeak.load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
}
} // namespace crack
