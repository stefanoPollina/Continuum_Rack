#pragma once
// Band-limited single-cycle tables for the rack's oscillators. Every waveform is defined the
// way Web Audio defines a PeriodicWave (cosine/sine coefficients per harmonic, normalised so
// the full-band waveform peaks at 1) and is rendered into octave-spaced mip levels so that
// no level holds harmonics above Nyquist for the notes that read it.
//
// Tables are built on a non-audio thread and never freed while the library lives, so voices
// can keep raw pointers to them across patch changes. No JUCE dependency.

#include "RackModel.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace crack
{
class MipTable
{
public:
    static constexpr int numLevels = 10;   // level L holds harmonics 1 .. 512 >> L

    // real[k], imag[k] for k = 1..H (index 0 ignored). Waveform = sum real cos + imag sin.
    MipTable (const std::vector<float>& real, const std::vector<float>& imag, bool normalise = true);

    // Reads the table at phase (0..1) for a note at freq Hz and sample rate sr.
    inline const std::vector<float>& levelFor (double freq, double sr) const noexcept
    {
        const double allowed = (0.5 * sr) / std::max (1.0, freq);
        int L = 0;
        while (L < numLevels - 1 && (double) maxH[(size_t) L] > allowed) ++L;
        return levels[(size_t) L];
    }

    static inline float read (const std::vector<float>& t, double phase) noexcept
    {
        const int n = (int) t.size() - 1;     // last sample is a wrap guard
        const double x = phase * n;
        const int i = (int) x;
        const float f = (float) (x - i);
        return t[(size_t) i] + f * (t[(size_t) i + 1] - t[(size_t) i]);
    }

    int harmonics() const noexcept { return maxH[0]; }

private:
    std::array<std::vector<float>, numLevels> levels;
    std::array<int, numLevels> maxH {};
};

// One Mosaic table: 17 frames of 512 harmonics, as the web app hands them to Web Audio.
struct MosaicTableInfo
{
    std::string id, name, bank;
    int frames = 0;
};

class WaveLibrary
{
public:
    WaveLibrary();

    // Decompressed contents of Resources/wavetables.bin.gz. Returns false if malformed.
    bool loadMosaicBank (const void* data, size_t size);

    const MipTable* basic (Wave) const noexcept;   // Web Audio's built-in oscillator shapes
    bool hasMosaic (const std::string& id) const;
    int mosaicFrameCount (const std::string& id) const;
    const std::vector<MosaicTableInfo>& mosaicTables() const noexcept { return infos; }

    // Built on first use; thread-safe; pointers stay valid for the library's lifetime.
    const MipTable* mosaic (const std::string& id, int frame);
    const MipTable* partials (const EngineEdit&);   // the eight authored harmonics

private:
    struct Frame { float scale; std::vector<int16_t> q; };   // q: re,im interleaved, k = 1..H
    std::array<std::unique_ptr<MipTable>, 4> basics;
    std::vector<MosaicTableInfo> infos;
    std::map<std::string, std::vector<Frame>> raw;
    int harmonicsPerFrame = 0;

    std::mutex lock;
    std::map<std::string, std::unique_ptr<MipTable>> built;
};
} // namespace crack
