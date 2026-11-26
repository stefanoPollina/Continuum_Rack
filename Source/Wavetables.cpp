#include "Wavetables.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>

namespace crack
{
namespace
{
constexpr double twoPi = 6.283185307179586476925286766559;

void inverseFFT (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = twoPi / (double) len;   // + sign: inverse transform
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// x[n] = sum_{k=1..H} re[k] cos(2 pi k n / N) + im[k] sin(2 pi k n / N)
std::vector<double> synthesise (const std::vector<float>& re, const std::vector<float>& im, int H, int N)
{
    std::vector<std::complex<double>> a ((size_t) N);
    for (int k = 1; k <= H && k < N / 2; ++k)
        a[(size_t) k] = { (double) re[(size_t) k], -(double) im[(size_t) k] };
    inverseFFT (a);
    std::vector<double> x ((size_t) N);
    for (int i = 0; i < N; ++i) x[(size_t) i] = a[(size_t) i].real();
    return x;
}

int levelSize (int maxH)
{
    int n = 256;
    while (n < 8 * maxH && n < 2048) n <<= 1;
    return n;
}

uint32_t rdU32 (const uint8_t*& p, const uint8_t* end, bool& ok)
{
    if (end - p < 4) { ok = false; return 0; }
    const uint32_t v = (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
    p += 4;
    return v;
}

std::string rdStr (const uint8_t*& p, const uint8_t* end, bool& ok)
{
    const uint32_t n = rdU32 (p, end, ok);
    if (! ok || (size_t) (end - p) < n || n > 4096) { ok = false; return {}; }
    std::string s ((const char*) p, n);
    p += n;
    return s;
}
} // namespace

MipTable::MipTable (const std::vector<float>& re, const std::vector<float>& im, bool normalise)
{
    const int H = (int) std::min (re.size(), im.size()) - 1;
    int top = 1;
    while (top < H) top <<= 1;
    top = std::min (top, 512);

    // Web Audio normalises a PeriodicWave by the peak of its full-band waveform; every
    // mip level shares that one scale so a note does not jump in level between octaves.
    double scale = 1.0;
    if (normalise)
    {
        const auto full = synthesise (re, im, H, 4096);
        double pk = 0;
        for (double v : full) pk = std::max (pk, std::abs (v));
        scale = pk > 1e-12 ? 1.0 / pk : 1.0;
    }

    for (int L = 0; L < numLevels; ++L)
    {
        const int lim = std::max (1, top >> L);
        maxH[(size_t) L] = std::min (lim, std::max (1, H));
        const int N = levelSize (lim);
        const auto x = synthesise (re, im, std::min (H, lim), N);
        auto& t = levels[(size_t) L];
        t.resize ((size_t) N + 1);
        for (int i = 0; i < N; ++i) t[(size_t) i] = (float) (x[(size_t) i] * scale);
        t[(size_t) N] = t[0];
    }
}

WaveLibrary::WaveLibrary()
{
    // Web Audio's built-in shapes, defined by their Fourier series and normalised.
    constexpr int H = 512;
    const double pi = 3.14159265358979323846;
    for (int w = 0; w < 4; ++w)
    {
        std::vector<float> re (H + 1, 0.0f), im (H + 1, 0.0f);
        for (int k = 1; k <= H; ++k)
        {
            double b = 0;
            switch ((Wave) w)
            {
                case Wave::sine:     b = k == 1 ? 1.0 : 0.0; break;
                case Wave::sawtooth: b = (k % 2 ? 2.0 : -2.0) / (pi * k); break;
                case Wave::square:   b = (k % 2) ? 4.0 / (pi * k) : 0.0; break;
                case Wave::triangle: b = (k % 2) ? 8.0 / (pi * pi * k * k) * ((((k - 1) / 2) % 2) ? -1.0 : 1.0) : 0.0; break;
            }
            im[(size_t) k] = (float) b;
        }
        basics[(size_t) w] = std::make_unique<MipTable> (re, im, true);
    }
}

const MipTable* WaveLibrary::basic (Wave w) const noexcept { return basics[(size_t) w].get(); }

bool WaveLibrary::loadMosaicBank (const void* data, size_t size)
{
    const uint8_t* p = (const uint8_t*) data;
    const uint8_t* end = p + size;
    bool ok = size >= 16 && std::memcmp (p, "CRWT", 4) == 0;
    if (! ok) return false;
    p += 4;
    const uint32_t version = rdU32 (p, end, ok), count = rdU32 (p, end, ok), H = rdU32 (p, end, ok);
    if (! ok || version != 1 || count > 4096 || H == 0 || H > 2048) return false;

    std::vector<MosaicTableInfo> newInfos;
    std::map<std::string, std::vector<Frame>> newRaw;
    for (uint32_t t = 0; t < count && ok; ++t)
    {
        MosaicTableInfo info;
        info.id = rdStr (p, end, ok);
        info.name = rdStr (p, end, ok);
        info.bank = rdStr (p, end, ok);
        const uint32_t frames = rdU32 (p, end, ok);
        if (! ok || frames == 0 || frames > 256) return false;
        info.frames = (int) frames;
        std::vector<Frame> fr (frames);
        for (auto& f : fr)
        {
            if ((size_t) (end - p) < 4 + (size_t) H * 4) return false;
            std::memcpy (&f.scale, p, 4);
            p += 4;
            f.q.resize ((size_t) H * 2);
            for (size_t i = 0; i < f.q.size(); ++i)
                f.q[i] = (int16_t) ((uint16_t) p[i * 2] | ((uint16_t) p[i * 2 + 1] << 8));
            p += (size_t) H * 4;
            if (! std::isfinite (f.scale)) return false;
        }
        newRaw[info.id] = std::move (fr);
        newInfos.push_back (std::move (info));
    }
    if (! ok) return false;
    std::lock_guard<std::mutex> g (lock);
    infos = std::move (newInfos);
    raw = std::move (newRaw);
    harmonicsPerFrame = (int) H;
    return true;
}

bool WaveLibrary::hasMosaic (const std::string& id) const { return ! id.empty() && raw.count (id) > 0; }

int WaveLibrary::mosaicFrameCount (const std::string& id) const
{
    auto it = raw.find (id);
    return it == raw.end() ? 0 : (int) it->second.size();
}

const MipTable* WaveLibrary::mosaic (const std::string& id, int frame)
{
    std::lock_guard<std::mutex> g (lock);
    auto it = raw.find (id);
    if (it == raw.end() || it->second.empty()) return nullptr;
    frame = std::min ((int) it->second.size() - 1, std::max (0, frame));
    const std::string key = "M:" + id + ":" + std::to_string (frame);
    if (auto b = built.find (key); b != built.end()) return b->second.get();

    const auto& f = it->second[(size_t) frame];
    const int H = harmonicsPerFrame;
    std::vector<float> re ((size_t) H + 1, 0.0f), im ((size_t) H + 1, 0.0f);
    for (int k = 1; k <= H; ++k)
    {
        re[(size_t) k] = f.q[(size_t) (k - 1) * 2] * f.scale;
        im[(size_t) k] = f.q[(size_t) (k - 1) * 2 + 1] * f.scale;
    }
    auto t = std::make_unique<MipTable> (re, im, true);
    auto* raw_ = t.get();
    built[key] = std::move (t);
    return raw_;
}

const MipTable* WaveLibrary::partials (const EngineEdit& e)
{
    // Same spectrum as the web app's "8 PARTIALS" PeriodicWave.
    std::vector<float> re (9, 0.0f), im (9, 0.0f);
    const double m = e.wtMorph / 100.0;
    std::string key = "P:";
    for (int h = 1; h <= 8; ++h)
    {
        const double authored = std::max (0.0f, e.wtH[(size_t) h - 1]) / 100.0;
        const double bright = 1.0 / std::sqrt ((double) h);
        const double tilt = std::pow ((double) h, (50.0 - e.wtTilt) / 55.0);
        const double balance = (h % 2) ? (0.5 + e.wtOdd / 100.0) : (1.5 - e.wtOdd / 100.0);
        im[(size_t) h] = (float) std::max (0.00001, ((1 - m) * authored + m * bright) * tilt * balance);
        char buf[32];
        std::snprintf (buf, sizeof buf, "%.6g,", (double) im[(size_t) h]);
        key += buf;
    }
    std::lock_guard<std::mutex> g (lock);
    if (auto b = built.find (key); b != built.end()) return b->second.get();
    auto t = std::make_unique<MipTable> (re, im, true);
    auto* raw_ = t.get();
    built[key] = std::move (t);
    return raw_;
}
} // namespace crack
