#include "RackModel.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace crack
{
namespace
{
float clampf (float v, float lo, float hi, float def)
{
    if (! std::isfinite (v)) return def;
    return std::min (hi, std::max (lo, v));
}

bool parseBankJack (const std::string& id, const char* prefix, int& index)
{
    if (id.size() != 4 || id.compare (0, 3, prefix) != 0) return false;
    const char c = id[3];
    if (c < '0' || c > '7') return false;
    index = c - '0';
    return true;
}

bool canConnect (const Cable& c)
{
    int a = 0, b = 0;
    if (parseBankJack (c.from, "bo:", a) && parseBankJack (c.to, "bi:", b))
        return a < b;
    return c.from == "lfo" && parseBankJack (c.to, "bm:", b);
}

const char* const cableColours[] = { "#f3374b", "#ffb437", "#00b56e", "#3695ef", "#8b4ade" };
} // namespace

RackState makeDefaultRack()
{
    RackState s;
    for (int b = 0; b < numBanks; ++b)
    {
        for (int i = 0; i < numSlots; ++i)
        {
            auto& slot = s.banks[(size_t) b].slots[(size_t) i];
            slot.on = (b == 0 && i == 0);
            slot.type = (b * 2 + i) % 8;
        }
        auto& p = s.patterns[(size_t) b];
        for (int i = 0; i < maxSteps; ++i)
            p.steps[(size_t) i].on = (b == 0) && i < 16 && i % 4 == 0;
    }
    return s;
}

void normalise (RackState& s)
{
    for (auto& b : s.banks)
    {
        b.level = clampf (b.level, 0, 100, 70);
        b.pan = clampf (b.pan, 0, 100, 50);
        b.cutoff = clampf (b.cutoff, 0, 100, 78);
        for (auto& sl : b.slots)
        {
            sl.type = std::min (7, std::max (0, sl.type));
            auto& e = sl.edit;
            e.mix = clampf (e.mix, 0, 100, 50);
            e.detune = clampf (e.detune, -100, 100, 0);
            e.transpose = clampf (e.transpose, -48, 48, 0);
            e.ratio = clampf (e.ratio, 0.05f, 16, 2);
            e.index = clampf (e.index, 0, 100, 35);
            e.pd = clampf (e.pd, 0, 100, 0);
            e.cutoff = clampf (e.cutoff, 0, 100, 85);
            e.resonance = clampf (e.resonance, 0, 100, 12);
            e.envDepth = clampf (e.envDepth, -100, 100, 0);
            e.lfoRate = clampf (e.lfoRate, 0.01f, 40, 2);
            e.lfoDepth = clampf (e.lfoDepth, 0, 100, 0);
            e.attack = clampf (e.attack, 0, 100, 12);
            e.decay = clampf (e.decay, 0, 100, 25);
            e.sustain = clampf (e.sustain, 0, 100, 65);
            e.release = clampf (e.release, 0, 100, 25);
            e.combFeedback = clampf (e.combFeedback, 0, 100, 25);
            e.wtMorph = clampf (e.wtMorph, 0, 100, 0);
            e.wtTilt = clampf (e.wtTilt, 0, 100, 50);
            e.wtOdd = clampf (e.wtOdd, 0, 100, 50);
            for (auto& h : e.wtH) h = clampf (h, 0, 100, 0);
            e.wtScan = clampf (e.wtScan, 0, 100, 0);
            e.wtSweep = clampf (e.wtSweep, -100, 100, 0);
        }
    }

    s.master = clampf (s.master, 0, 100, 55);
    s.drive = clampf (s.drive, 0, 100, 0);
    s.delayMix = clampf (s.delayMix, 0, 65, 12);
    s.delayTime = clampf (s.delayTime, 0.06f, 0.8f, 0.25f);
    s.feedback = clampf (s.feedback, 0, 70, 12);
    s.lfoRate = clampf (s.lfoRate, 0.05f, 15, 2);
    s.lfoDepth = clampf (s.lfoDepth, 0, 100, 20);
    s.tempo = clampf (s.tempo, 40, 240, 120);
    s.root = clampf (s.root, 24, 107, 48);
    s.sendLevel = clampf (s.sendLevel, 0, 40, 8);

    for (auto& p : s.patterns)
    {
        if (p.length != 8 && p.length != 16 && p.length != 32 && p.length != 64) p.length = 16;
        for (auto& st : p.steps)
        {
            st.pitch = clampf (st.pitch, -24, 24, 0);
            st.velocity = clampf (st.velocity, 1, 127, 100);
            st.gate = clampf (st.gate, 5, 3200, 55);
            st.filter = clampf (st.filter, 5, 100, 78);
        }
    }

    // Cables: valid only; the last cable into an input wins; at most 12 sends (the last 12).
    std::vector<Cable> kept;
    std::set<std::string> seen;
    int sends = 0;
    for (auto it = s.cables.rbegin(); it != s.cables.rend(); ++it)
    {
        if (! canConnect (*it) || seen.count (it->to)) continue;
        if (it->from.compare (0, 2, "bo") == 0 && ++sends > maxSends) continue;
        seen.insert (it->to);
        Cable c = *it;
        if (std::find (std::begin (cableColours), std::end (cableColours), c.color) == std::end (cableColours))
            c.color = cableColours[0];
        kept.push_back (c);
    }
    std::reverse (kept.begin(), kept.end());
    s.cables = std::move (kept);
}

Routing computeRouting (const RackState& s)
{
    Routing r;
    bool anyLfo = false;
    for (const auto& c : s.cables)
    {
        int a = 0, b = 0;
        if (parseBankJack (c.from, "bo:", a) && parseBankJack (c.to, "bi:", b) && a < b)
        {
            r.sends.push_back ({ a, b });
            r.stereoInput[(size_t) b] = true;
        }
        else if (c.from == "lfo" && parseBankJack (c.to, "bm:", b))
        {
            r.lfoTarget[(size_t) b] = true;
            anyLfo = true;
        }
    }
    if (! anyLfo) r.lfoTarget.fill (true);   // unpatched LFO reaches every bank
    std::stable_sort (r.sends.begin(), r.sends.end(), [] (auto x, auto y) { return x.from < y.from; });
    return r;
}

const char* waveName (Wave w)
{
    switch (w) { case Wave::sine: return "sine"; case Wave::triangle: return "triangle"; case Wave::sawtooth: return "sawtooth"; case Wave::square: return "square"; }
    return "sine";
}
const char* voiceFilterName (VoiceFilter f)
{
    switch (f) { case VoiceFilter::lowpass: return "lowpass"; case VoiceFilter::highpass: return "highpass"; case VoiceFilter::bandpass: return "bandpass"; case VoiceFilter::notch: return "notch"; case VoiceFilter::comb: return "comb"; }
    return "lowpass";
}
const char* lfoTargetName (LfoTarget t)
{
    switch (t) { case LfoTarget::filter: return "filter"; case LfoTarget::pitch: return "pitch"; case LfoTarget::amplitude: return "amplitude"; }
    return "filter";
}
const char* insertName (Insert i)
{
    switch (i) { case Insert::none: return "none"; case Insert::drive: return "drive"; case Insert::lowpass: return "lowpass"; case Insert::highpass: return "highpass"; case Insert::ring: return "ring"; }
    return "none";
}
const char* insertLabel (Insert i)
{
    switch (i) { case Insert::none: return "BYPASS"; case Insert::drive: return "SATUR"; case Insert::lowpass: return "DARK"; case Insert::highpass: return "HPF"; case Insert::ring: return "TREM"; }
    return "BYPASS";
}
const char* lfoDestName (LfoDest d)
{
    switch (d) { case LfoDest::filter: return "filter"; case LfoDest::amp: return "amp"; case LfoDest::pan: return "pan"; }
    return "filter";
}
const char* arpName (Arp a)
{
    switch (a) { case Arp::off: return "off"; case Arp::up: return "up"; case Arp::down: return "down"; case Arp::updown: return "updown"; case Arp::random: return "random"; }
    return "off";
}
const char* engineShortName (int t)
{
    static const char* n[] = { "ACID", "HYBRD", "FM", "FOLD", "WTBL", "RESON", "ADDTV", "XMOD" };
    return n[std::min (7, std::max (0, t))];
}
const char* engineTitle (int t)
{
    static const char* n[] = { "ACID 303", "HYBRID", "FM MATRIX", "WAVEFOLD", "WAVETBL", "RESONATR", "ADDITIVE", "CROSSMOD" };
    return n[std::min (7, std::max (0, t))];
}

template <typename E, size_t N>
static bool parseEnum (const std::string& s, E& out, const char* const (&names)[N])
{
    for (size_t i = 0; i < N; ++i)
        if (s == names[i]) { out = (E) i; return true; }
    return false;
}

bool parseWave (const std::string& s, Wave& w)
{
    static const char* const n[] = { "sine", "triangle", "sawtooth", "square" };
    return parseEnum (s, w, n);
}
bool parseVoiceFilter (const std::string& s, VoiceFilter& f)
{
    static const char* const n[] = { "lowpass", "highpass", "bandpass", "notch", "comb" };
    return parseEnum (s, f, n);
}
bool parseLfoTarget (const std::string& s, LfoTarget& t)
{
    static const char* const n[] = { "filter", "pitch", "amplitude" };
    return parseEnum (s, t, n);
}
bool parseInsert (const std::string& s, Insert& i)
{
    static const char* const n[] = { "none", "drive", "lowpass", "highpass", "ring" };
    return parseEnum (s, i, n);
}
bool parseLfoDest (const std::string& s, LfoDest& d)
{
    static const char* const n[] = { "filter", "amp", "pan" };
    return parseEnum (s, d, n);
}
bool parseArp (const std::string& s, Arp& a)
{
    static const char* const n[] = { "off", "up", "down", "updown", "random" };
    return parseEnum (s, a, n);
}
} // namespace crack
