#pragma once

// BEQ's spectrum analyser: a tap the audio thread writes into, and the analysis the
// window does with it (FFT, long-term average, live line, slope, fractional-octave
// smoothing). Plain C++ so it can be tested without JUCE. The Chrome extension's
// spectrum.js does the same display maths; keep the two in step.
//
// Costs nothing while nobody is looking: the audio thread only copies samples while
// a view has registered itself (addViewer), and all the maths runs in the view.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace bteq::spectrum {

struct Settings {
    int where = 0;          // 0: before any EQ; 1: after the correction (the output); 2: with only My EQ
    int view = 0;           // 0: live + average, 1: average, 2: live
    int window = 0;         // 0: Hann, 1: Blackman-Harris
    double avgMs = 6000;    // 0: average until reset
    int block = 8192;
    double overlap = 0.88;
    double slope = 4.5;     // dB per octave, around 1 kHz
    double smooth = 2.0 / 3.0; // octaves; 0 = off
    double lo = -69, hi = -12; // dB shown, bottom to top
    double fLo = 20, fHi = 20000;
    int open = 1;           // the pop-out's settings strip is showing

    std::string toString() const {
        char b[256];
        std::snprintf(b, sizeof b, "%d %d %d %g %d %g %g %g %g %g %g %g %d", where, view, window, avgMs, block, overlap, slope,
                      smooth, lo, hi, fLo, fHi, open);
        return b;
    }
    static Settings fromString(const std::string& s) {
        Settings d, r;
        std::istringstream in(s);
        if (!(in >> r.where >> r.view >> r.window >> r.avgMs >> r.block >> r.overlap >> r.slope >> r.smooth >> r.lo >> r.hi >> r.fLo >> r.fHi))
            return d;
        if (!(in >> r.open)) r.open = 1;
        r.open = r.open ? 1 : 0;
        r.where = std::clamp(r.where, 0, 2);
        r.view = std::clamp(r.view, 0, 2);
        r.window = std::clamp(r.window, 0, 1);
        r.avgMs = std::clamp(r.avgMs, 0.0, 120000.0);
        if (r.block < 1024 || r.block > 32768 || (r.block & (r.block - 1)) != 0) r.block = d.block;
        r.overlap = std::clamp(r.overlap, 0.0, 0.95);
        r.slope = std::clamp(r.slope, -6.0, 12.0);
        r.smooth = std::clamp(r.smooth, 0.0, 2.5);
        r.lo = std::clamp(r.lo, -160.0, 20.0);
        r.hi = std::clamp(r.hi, r.lo + 6, 30.0);
        r.fLo = std::clamp(r.fLo, 10.0, 10000.0);
        r.fHi = std::clamp(r.fHi, r.fLo * 2, 24000.0);
        return r;
    }
};

// The smoothing choices, in octaves (the same steps as SPAN); 0 = off.
struct SmoothStep { double oct; const char* name; };
inline const std::vector<SmoothStep>& smoothSteps() {
    static const std::vector<SmoothStep> steps { { 0, "Off" },         { 1.0 / 48, "1/48" }, { 1.0 / 24, "1/24" }, { 1.0 / 12, "1/12" },
                                                 { 1.0 / 9, "1/9" },   { 1.0 / 6, "1/6" },   { 1.0 / 5, "1/5" },   { 1.0 / 4, "1/4" },
                                                 { 1.0 / 3, "1/3" },   { 1.0 / 2, "1/2" },   { 2.0 / 3, "2/3" },   { 3.0 / 4, "3/4" },
                                                 { 1, "1" },           { 2, "2" },           { 2.5, "2.5" } };
    return steps;
}
// The step nearest to `oct` (a value saved before the steps existed lands on the closest one).
inline int smoothIndex(double oct) {
    const auto& st = smoothSteps();
    int best = 0;
    for (int i = 1; i < (int)st.size(); ++i)
        if (std::abs(st[(size_t)i].oct - oct) < std::abs(st[(size_t)best].oct - oct)) best = i;
    return best;
}

// Samples for the analyser, written by the audio thread (mono: the channels averaged).
// The buffer is made the first time a view asks for it and kept until the processor
// goes; while no view is registered the audio thread skips it entirely.
class Tap {
public:
    static constexpr int kSize = 1 << 16; // more than the biggest block, so a read never races the writer

    void addViewer() {
        if (buf.load(std::memory_order_acquire) == nullptr) {
            storage.reset(new float[kSize]());
            buf.store(storage.get(), std::memory_order_release);
        }
        viewers.fetch_add(1, std::memory_order_acq_rel);
    }
    void removeViewer() { viewers.fetch_sub(1, std::memory_order_acq_rel); }
    bool wanted() const { return viewers.load(std::memory_order_acquire) > 0; }

    // Audio thread.
    void write(const float* const* ch, int chans, int n) {
        float* b = buf.load(std::memory_order_acquire);
        if (b == nullptr || chans <= 0) return;
        uint64_t pos = written.load(std::memory_order_relaxed);
        const float scale = 1.0f / (float)chans;
        for (int i = 0; i < n; ++i) {
            float s = 0;
            for (int c = 0; c < chans; ++c) s += ch[c][i];
            b[(pos + (uint64_t)i) & (kSize - 1)] = s * scale;
        }
        written.store(pos + (uint64_t)n, std::memory_order_release);
    }

    uint64_t total() const { return written.load(std::memory_order_acquire); }
    // The `count` samples that end at `end` (a position counted like total()).
    bool read(float* dest, int count, uint64_t end) const {
        const float* b = buf.load(std::memory_order_acquire);
        if (b == nullptr || count > kSize / 2 || end < (uint64_t)count || total() - end > (uint64_t)(kSize / 2)) return false;
        for (int i = 0; i < count; ++i) dest[i] = b[(end - (uint64_t)count + (uint64_t)i) & (kSize - 1)];
        return true;
    }

    std::atomic<int> where { 0 }; // which point in the chain the audio thread taps (0 input, 1 output)
    static int tapPoint(int settingsWhere) { return settingsWhere == 1 ? 1 : 0; } // "with only My EQ" is worked out from the input
    // Fed from outside instead (the standalone's loopback): the audio thread leaves
    // it alone, and `rate` is that source's sample rate. `generation` changes
    // whenever the source does, so views start their average again.
    std::atomic<bool> external { false };
    std::atomic<double> rate { 0.0 };
    std::atomic<int> generation { 0 };

private:
    std::atomic<int> viewers { 0 };
    std::atomic<float*> buf { nullptr };
    std::unique_ptr<float[]> storage;
    std::atomic<uint64_t> written { 0 };
};

// In-place radix-2 FFT.
inline void fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2 * 3.14159265358979323846 / (double)len;
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1);
            for (size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

class Analyser {
public:
    Settings settings;
    // Optional: dB to add at each FFT bin's frequency, applied to the power before
    // any smoothing (so narrow cuts come out exactly). Return empty for none. The
    // standalone uses it to take BEQ's EQ back out of what Windows plays.
    std::function<std::vector<double>(const std::vector<double>& binFreqs)> binOffsetDb;

    void reset() { avg.clear(); live.clear(); frames = 0; }
    bool hasData() const { return !avg.empty() && frames > 0; }

    // Reads whatever the tap has gathered since last time: one FFT per hop
    // (block x (1 - overlap) samples), at most `maxFrames` per call.
    void process(const Tap& tap, double rate, int maxFrames = 8) {
        sampleRate = rate;
        const int n = settings.block;
        if ((int)window.size() != n || windowType != settings.window) makeWindow();
        const int hop = std::max(64, (int)std::lround(n * (1.0 - settings.overlap)));
        const uint64_t total = tap.total();
        if (total < (uint64_t)n) return;
        // Too far behind (the view was hidden): start from the latest audio.
        if (next == 0 || total - next > (uint64_t)(Tap::kSize / 4) || next > total) next = total;
        int done = 0;
        while (next + (uint64_t)hop <= total + (uint64_t)hop && next <= total && done < maxFrames) {
            if (!tap.read(frame.data(), n, next)) break;
            addFrame((double)hop / rate);
            next += (uint64_t)hop;
            ++done;
        }
    }

    // Level in dB at `points` frequencies from fLo to fHi (log spaced), with slope
    // and smoothing. `average`: the long-term average, else the live line.
    std::vector<double> curve(bool average, int points) const {
        std::vector<double> out((size_t)std::max(points, 2), settings.lo - 20);
        const auto& raw = average ? avg : live;
        if (raw.empty() || points < 2) return out;
        const size_t nb = raw.size();
        const double hz = sampleRate / (2.0 * (double)nb);
        std::vector<double> adjusted;
        if (binOffsetDb) {
            std::vector<double> bf(nb);
            for (size_t i = 0; i < nb; ++i) bf[i] = (double)i * hz;
            const auto off = binOffsetDb(bf);
            if (off.size() == nb) {
                adjusted.resize(nb);
                for (size_t i = 0; i < nb; ++i) adjusted[i] = raw[i] * std::pow(10.0, off[i] / 10.0);
            }
        }
        const auto& power = adjusted.empty() ? raw : adjusted;
        std::vector<double> sum(nb + 1, 0.0);
        for (size_t i = 0; i < nb; ++i) sum[i + 1] = sum[i] + power[i];
        const double span = std::log2(settings.fHi / settings.fLo), step = span / (points - 1);
        std::vector<double> pts((size_t)points), freq((size_t)points);
        for (int j = 0; j < points; ++j) {
            const double f = settings.fLo * std::pow(2.0, step * j);
            freq[(size_t)j] = f;
            const double b0 = f * std::pow(2.0, -step / 2) / hz, b1 = f * std::pow(2.0, step / 2) / hz;
            const size_t i0 = (size_t)std::max(0.0, std::floor(b0)), i1 = std::min(nb, (size_t)std::ceil(b1));
            double p;
            if (b1 - b0 < 1 || i1 <= i0) { // narrower than a bin: between the two nearest bins
                const double x = std::clamp(f / hz, 0.0, (double)nb - 1.001);
                const size_t i = (size_t)x;
                const double t = x - (double)i;
                p = power[i] * (1 - t) + power[i + 1] * t;
            } else if (settings.smooth > 0) {
                p = (sum[i1] - sum[i0]) / (double)(i1 - i0);
            } else {
                p = 0; // off: the loudest bin, so peaks keep their height
                for (size_t i = i0; i < i1; ++i) p = std::max(p, power[i]);
            }
            pts[(size_t)j] = p;
        }
        if (settings.smooth > 0) {
            // Gaussian over log frequency; -6 dB width about `smooth` octaves.
            const double sigma = settings.smooth / 2.355 / step;
            const int reach = (int)std::ceil(sigma * 3);
            std::vector<double> k((size_t)(2 * reach + 1));
            for (int i = -reach; i <= reach; ++i) k[(size_t)(i + reach)] = std::exp(-(double)(i * i) / (2 * sigma * sigma));
            std::vector<double> sm((size_t)points);
            for (int j = 0; j < points; ++j) {
                double a = 0, w = 0;
                for (int i = std::max(-reach, -j); i <= reach && j + i < points; ++i) {
                    a += pts[(size_t)(j + i)] * k[(size_t)(i + reach)];
                    w += k[(size_t)(i + reach)];
                }
                sm[(size_t)j] = a / w;
            }
            pts.swap(sm);
        }
        for (int j = 0; j < points; ++j)
            out[(size_t)j] = 10 * std::log10(std::max(1e-30, pts[(size_t)j])) + settings.slope * std::log2(freq[(size_t)j] / 1000.0);
        return out;
    }

private:
    void makeWindow() {
        const int n = settings.block;
        window.assign((size_t)n, 0.0);
        double sum = 0;
        for (int i = 0; i < n; ++i) {
            const double x = 2 * 3.14159265358979323846 * i / (n - 1);
            window[(size_t)i] = settings.window == 1 ? 0.35875 - 0.48829 * std::cos(x) + 0.14128 * std::cos(2 * x) - 0.01168 * std::cos(3 * x)
                                                     : 0.5 - 0.5 * std::cos(x);
            sum += window[(size_t)i];
        }
        norm = 2.0 / sum; // a full-scale sine reads 0 dB
        windowType = settings.window;
        frame.assign((size_t)n, 0.0f);
        buf.assign((size_t)n, {});
        reset();
    }

    void addFrame(double dt) {
        const size_t n = window.size(), nb = n / 2;
        for (size_t i = 0; i < n; ++i) buf[i] = { frame[i] * window[i], 0.0 };
        fft(buf);
        if (avg.size() != nb) { avg.assign(nb, 0.0); live.assign(nb, 0.0); frames = 0; }
        ++frames;
        const double a = settings.avgMs > 0 ? 1 - std::exp(-dt / (settings.avgMs / 1000.0)) : 1.0 / (double)frames;
        const double fall = std::exp(-dt / 0.25); // live: jumps up, falls back over about a quarter second
        for (size_t i = 0; i < nb; ++i) {
            const double p = std::norm(buf[i]) * norm * norm;
            avg[i] = frames == 1 ? p : avg[i] + a * (p - avg[i]);
            live[i] = std::max(p, live[i] * fall);
        }
    }

    double sampleRate = 48000, norm = 1;
    int windowType = -1;
    uint64_t next = 0;
    std::vector<double> window, avg, live;
    std::vector<float> frame;
    std::vector<std::complex<double>> buf;
    long frames = 0;
};

} // namespace bteq::spectrum
