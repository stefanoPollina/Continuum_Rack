#pragma once
// Plain data model of a Continuum Rack patch. Field names, ranges and defaults follow the
// web app's state (format "continuum-synth-p01") so patches round-trip between the two.
// No JUCE dependency.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace crack
{
constexpr int numBanks = 8;
constexpr int numSlots = 8;
constexpr int maxSteps = 64;
constexpr int maxSends = 12;

enum class Wave : uint8_t { sine, triangle, sawtooth, square };
enum class VoiceFilter : uint8_t { lowpass, highpass, bandpass, notch, comb };
enum class LfoTarget : uint8_t { filter, pitch, amplitude };
enum class Insert : uint8_t { none, drive, lowpass, highpass, ring };
enum class LfoDest : uint8_t { filter, amp, pan };
enum class Arp : uint8_t { off, up, down, updown, random };

// Engine types, in the web app's order.
enum EngineType { acid = 0, hybrid, fmMatrix, wavefold, wavetable, resonator, additive, crossmod };

struct EngineEdit
{
    Wave wave1 = Wave::sawtooth, wave2 = Wave::sine;
    float mix = 50, detune = 0, transpose = 0, ratio = 2, index = 35, pd = 0;
    float cutoff = 85, resonance = 12;
    VoiceFilter filter = VoiceFilter::lowpass;
    float envDepth = 0, lfoRate = 2, lfoDepth = 0;
    LfoTarget lfoTarget = LfoTarget::filter;
    float attack = 12, decay = 25, sustain = 65, release = 25, combFeedback = 25;
    bool wtEnabled = false;
    float wtMorph = 0, wtTilt = 50, wtOdd = 50;
    std::array<float, 8> wtH { 100, 40, 28, 20, 14, 10, 8, 6 };
    std::string wtTable;   // "" = the eight authored partials
    float wtScan = 0, wtSweep = 0;
};

struct Slot
{
    bool on = false;
    int type = 0;
    EngineEdit edit;
};

struct Bank
{
    float level = 70, pan = 50, cutoff = 78;
    Insert insert = Insert::none;
    std::array<Slot, numSlots> slots;
};

struct Step
{
    bool on = false;
    float pitch = 0, velocity = 100, gate = 55, filter = 78;
};

struct Pattern
{
    int length = 16;
    bool mute = false;
    Arp arp = Arp::off;
    std::array<Step, maxSteps> steps;
};

struct EngineSequence
{
    std::array<bool, numSlots> targets {};
    std::array<std::array<bool, maxSteps>, numSlots> masks {};
    EngineSequence()
    {
        targets.fill (true);
        for (auto& m : masks) m.fill (true);
    }
};

// Cable jack ids as the web app writes them: "bo:N", "bi:N", "bm:N", "lfo".
struct Cable
{
    std::string from, to, color;
};

struct RackState
{
    std::array<Bank, numBanks> banks;
    std::array<Pattern, numBanks> patterns;
    std::array<EngineSequence, numBanks> engineSequence;
    std::vector<Cable> cables;

    float master = 55, drive = 0, delayMix = 12, delayTime = 0.25f, feedback = 12;
    float lfoRate = 2, lfoDepth = 20;
    LfoDest lfoDest = LfoDest::filter;
    Wave lfoWave = Wave::sine;
    float tempo = 120, root = 48, sendLevel = 8;
};

// The web app's defaults for a fresh rack (defaultBanks / initPattern).
RackState makeDefaultRack();

// Applies the web app's normalize() rules: clamps every value to its range and drops
// cables that are not forward bank sends or LFO→MOD patches (last cable to an input wins,
// at most 12 sends).
void normalise (RackState&);

// Derived routing, computed from cables.
struct Routing
{
    struct Send { int from, to; };
    std::vector<Send> sends;                 // forward only, from < to
    std::array<bool, numBanks> lfoTarget {}; // true where the global LFO reaches a bank
    std::array<bool, numBanks> stereoInput {}; // bank receives a (stereo) send
};
Routing computeRouting (const RackState&);

// Name tables, used by the JSON layer and the editor.
const char* waveName (Wave);
const char* voiceFilterName (VoiceFilter);
const char* lfoTargetName (LfoTarget);
const char* insertName (Insert);
const char* insertLabel (Insert);   // panel label: BYPASS, SATUR, DARK, HPF, TREM
const char* lfoDestName (LfoDest);
const char* arpName (Arp);
const char* engineShortName (int type); // ACID, HYBRD, ...
const char* engineTitle (int type);     // ACID 303, HYBRID, ...

bool parseWave (const std::string&, Wave&);
bool parseVoiceFilter (const std::string&, VoiceFilter&);
bool parseLfoTarget (const std::string&, LfoTarget&);
bool parseInsert (const std::string&, Insert&);
bool parseLfoDest (const std::string&, LfoDest&);
bool parseArp (const std::string&, Arp&);
} // namespace crack
