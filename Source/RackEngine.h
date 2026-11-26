#pragma once
// The Continuum Rack audio engine: a C++ port of the web app's Web Audio graph.
//
//   64 engine slots (8 banks x 8) -> per-bank strip (filter, insert shaper, tremolo, level,
//   pan, trim) -> forward bank sends -> master drive + echo -> compressor -> master level
//
// Voice DSP follows the web app's voice() function node for node: two oscillators, optional
// Mosaic wavetable hops or eight-partial wave, FM, fold shaper, resonator and additive
// partials, a Web Audio biquad (or comb), per-voice LFO and the same envelope automation.
// No JUCE dependency; process() does not allocate or lock.

#include "RackModel.h"
#include "Wavetables.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace crack
{
// Everything a voice needs from its slot that is resolved ahead of time (tables).
struct SlotSetup
{
    const MipTable* wave1 = nullptr;           // osc 1: basic wave, partials or table frame
    const MipTable* wave2 = nullptr;
    std::array<const MipTable*, 5> hop {};     // Mosaic frames crossfaded over attack+decay
    int hops = 0;                              // number of crossfades (0..4)
};

// An immutable snapshot of a rack patch, built off the audio thread.
struct Program
{
    RackState state;
    Routing routing;
    std::array<std::array<SlotSetup, numSlots>, numBanks> setup;
};

std::unique_ptr<Program> buildProgram (const RackState&, WaveLibrary&);

// Values the host can automate. They override the matching fields of the program's state.
struct LiveParams
{
    float master = 55, drive = 0, delayMix = 12, delayTime = 0.25f, feedback = 12;
    float lfoRate = 2, lfoDepth = 20;
    LfoDest lfoDest = LfoDest::filter;
    Wave lfoWave = Wave::sine;
    float sendLevel = 8, root = 48, tempo = 120;
    std::array<float, numBanks> level {}, pan {}, cutoff {};
    std::array<bool, numBanks> seqMute {};

    static LiveParams fromState (const RackState&);
};

enum class SeqMode { host, internal, off };
enum class MidiGate { gated, oneShot };

struct EngineOptions
{
    SeqMode seqMode = SeqMode::host;
    bool internalRun = false;
    MidiGate midiGate = MidiGate::gated;
    int midiBank = -1;              // -1 = every enabled engine in the rack, else one bank
};

struct Transport
{
    bool playing = false;
    bool hasPosition = false;       // ppq and bpm both valid
    double ppq = 0;                 // quarter notes since song start, at the block's first sample
    double bpm = 120;
};

struct MidiEvent
{
    enum Type : uint8_t { noteOn, noteOff, sustain, allNotesOff } type;
    int offset;                     // sample within the block
    int note;
    float velocity;                 // 0..1
};

class RackEngine
{
public:
    static constexpr int maxVoices = 256;
    static constexpr int controlInterval = 16;   // samples between modulation updates

    RackEngine();
    ~RackEngine();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();                   // silence all voices and tails, rewind internal clock

    // Program swaps are taken at the next block start; the engine never frees programs.
    void setProgram (const Program* p) noexcept { program = p; }
    const Program* getProgram() const noexcept { return program; }

    void process (float* left, float* right, int numSamples, const LiveParams&, const EngineOptions&,
                  const Transport&, const MidiEvent* events, int numEvents) noexcept;

    // Meters and activity for the editor (relaxed atomics).
    std::array<std::atomic<float>, numBanks> bankPeak {};
    std::atomic<float> masterPeak { 0 };
    std::array<std::array<std::atomic<uint32_t>, numSlots>, numBanks> slotHits {};
    std::array<std::atomic<int>, numBanks> playStep {};   // -1 when idle
    std::atomic<int> activeVoices { 0 };
    std::atomic<bool> usingHostClock { false };

    int countActiveVoices() const noexcept;
    // The master compressor's 6 ms look-ahead (Chromium's DynamicsCompressor pre-delay).
    int getLatencySamples() const noexcept { return (int) std::lround (0.006 * sr); }

    struct Voice;
    struct Bus;
    struct Master;

private:
    void startVoice (int bank, int slot, int midi, double duration, float velocity, int offset, int noteId, bool gated) noexcept;
    void releaseNote (int note, int offset) noexcept;
    void runStep (int64_t tick, int offset, double bpm, const LiveParams&) noexcept;
    void midiNote (int note, float velocity, int offset, const EngineOptions&) noexcept;

    const Program* program = nullptr;
    double sr = 48000;
    int maxBlock = 0;
    std::vector<Voice> voices;
    std::unique_ptr<Bus[]> buses;
    std::unique_ptr<Master> master;

    // global modulators
    double lfoPhase = 0, tremPhase = 0;
    float lfoGainValue = 0;
    std::vector<float> lfoBuf, tremBuf;

    // sequencer
    int64_t internalTick = 0;
    double internalPhase = 0;       // fraction of a 16th already elapsed
    bool internalWasRunning = false;
    uint32_t rng = 0x9e3779b9u;
    bool sustainDown = false;
    std::array<bool, 128> heldBySustain {};
    float lastCutoff[numBanks] {};
    uint64_t voiceSerial = 0;
    const Program* lastProgram = nullptr;
};
} // namespace crack
