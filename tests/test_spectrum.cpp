// Checks BEQ's spectrum analyser: levels, averaging, slope, smoothing and the tap.
#include "../src/plugin/Spectrum.h"

#include <cstdio>
#include <string>

using namespace bteq::spectrum;

static int failures = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++failures; }
}

static void feed(Tap& tap, double rate, double seconds, double freq, double amp, double noise = 0) {
    const int block = 512;
    std::vector<float> l(block), r(block);
    static uint32_t seed = 1;
    static double phase = 0;
    for (int done = 0; done < (int)(seconds * rate); done += block) {
        for (int i = 0; i < block; ++i) {
            seed = seed * 1664525u + 1013904223u;
            const double nz = noise * (((seed >> 8) / 16777216.0) * 2 - 1);
            l[i] = r[i] = (float)(amp * std::sin(phase) + nz);
            phase += 2 * 3.14159265358979323846 * freq / rate;
        }
        const float* ch[2] = { l.data(), r.data() };
        tap.write(ch, 2, block);
    }
}

static double at(const Analyser& a, double f, bool average = true) {
    const int points = 400;
    const auto c = a.curve(average, points);
    const double span = std::log2(a.settings.fHi / a.settings.fLo);
    const int j = (int)std::lround(std::log2(f / a.settings.fLo) / span * (points - 1));
    double best = -1e9;
    for (int k = std::max(0, j - 2); k <= std::min(points - 1, j + 2); ++k) best = std::max(best, c[(size_t)k]);
    return best;
}

int main() {
    const double rate = 48000;
    {
        Tap tap;
        tap.addViewer();
        Analyser a;
        a.settings.slope = 0;
        a.settings.smooth = 0;
        a.settings.avgMs = 1000;
        for (int i = 0; i < 40; ++i) { feed(tap, rate, 0.05, 1000, 1.0); a.process(tap, rate); }
        const double db = at(a, 1000);
        std::printf("full-scale 1 kHz sine reads %.2f dB\n", db);
        check(std::abs(db) < 1.6, "full-scale sine reads about 0 dB (Hann scalloping at most 1.5 dB)");
        check(at(a, 100) < db - 60, "nothing at 100 Hz");
        const double half = [&] { Analyser b; b.settings = a.settings; Tap t; t.addViewer();
                                  for (int i = 0; i < 40; ++i) { feed(t, rate, 0.05, 1000, 0.5); b.process(t, rate); }
                                  return at(b, 1000); }();
        check(std::abs((db - half) - 6.02) < 0.2, "half the level reads 6 dB lower");
        // Slope tilts around 1 kHz.
        a.settings.slope = 4.5;
        check(std::abs(at(a, 1000) - db) < 0.3, "slope leaves 1 kHz alone");
        // Smoothing spreads a tone and lowers its peak.
        a.settings.slope = 0;
        a.settings.smooth = 2.0 / 3.0;
        check(at(a, 1000) < db - 3 && at(a, 1100) > at(a, 100) + 20, "smoothing spreads the tone");
    }
    {
        // The average follows a level change over its time; live jumps straight away.
        Tap tap;
        tap.addViewer();
        Analyser a;
        a.settings.slope = 0;
        a.settings.smooth = 0;
        a.settings.avgMs = 6000;
        for (int i = 0; i < 60; ++i) { feed(tap, rate, 0.05, 1000, 0.1); a.process(tap, rate); }
        const double quiet = at(a, 1000);
        for (int i = 0; i < 10; ++i) { feed(tap, rate, 0.05, 1000, 1.0); a.process(tap, rate); }
        const double avgAfter = at(a, 1000), liveAfter = at(a, 1000, false);
        std::printf("after 0.5 s 20 dB louder: average %+.1f dB, live %+.1f dB\n", avgAfter - quiet, liveAfter - quiet);
        check(liveAfter - quiet > 19, "live jumps up");
        check(avgAfter - quiet > 1 && avgAfter - quiet < 12, "average moves slowly");
        a.reset();
        check(!a.hasData(), "reset clears");
    }
    {
        Settings s;
        s.lo = -70; s.hi = -10; s.block = 16384; s.avgMs = 0;
        const auto t = Settings::fromString(s.toString());
        check(t.toString() == s.toString(), "settings round trip");
        check(Settings::fromString("junk").block == 8192, "bad settings give defaults");
        Tap tap;
        float x[4];
        check(!tap.read(x, 4, 4), "no buffer before a viewer");
        check(!tap.wanted(), "nobody wants it at first");
    }
    {
        using bteq::spectrum::smoothIndex;
        using bteq::spectrum::smoothSteps;
        check(smoothSteps().size() == 15 && smoothSteps().back().oct == 2.5, "smoothing steps like SPAN, up to 2.5 oct");
        check(smoothIndex(0) == 0 && smoothIndex(2.0 / 3.0) == 10 && smoothIndex(1.0 / 9.0) == 4, "steps found by value");
        check(smoothIndex(5.0 / 24.0) == 6, "an old in-between value lands on the nearest step");
        Settings s;
        s.smooth = 2.5;
        check(Settings::fromString(s.toString()).smooth == 2.5, "2.5 oct survives a save");
    }
    {
        const double rate = 48000;
        Tap tap;
        tap.addViewer();
        Analyser a;
        a.settings.slope = 0;
        a.settings.smooth = 0;
        for (int i = 0; i < 20; ++i) { feed(tap, rate, 0.05, 1000, 1.0); a.process(tap, rate); }
        const double plain = at(a, 1000);
        a.adjust = [](const std::vector<double>&, std::vector<double>& db) { for (auto& d : db) d -= 6; };
        check(std::abs(at(a, 1000) - plain + 6) < 0.01, "adjust hook changes the curve");
    }
    if (failures == 0) std::printf("spectrum: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
