// Renders the impulse response of a ParametricEQ preset through the DSP core.
// Usage: render_impulse <preset.txt> <sampleRate> <amount> <out.raw>
#include "../src/dsp/ParametricEq.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 5) { std::fprintf(stderr, "usage: %s preset fs amount out\n", argv[0]); return 2; }
    auto preset = bteq::loadParametricFile(argv[1]);
    if (!preset) { std::fprintf(stderr, "could not parse preset\n"); return 1; }
    const double fs = std::atof(argv[2]), amount = std::atof(argv[3]);
    bteq::ParametricEq eq;
    eq.prepare(*preset, fs, 1, amount, /*autoPreamp*/ false);
    std::vector<double> buf(1 << 18, 0.0);
    buf[0] = 1.0;
    eq.processChannel(0, buf.data(), (int)buf.size());
    FILE* f = std::fopen(argv[4], "wb");
    std::fwrite(buf.data(), sizeof(double), buf.size(), f);
    std::fclose(f);
    std::printf("filters=%zu filePreamp=%.2f autoPreampWouldBe=%.2f\n", preset->filters.size(),
                preset->preampDb, -std::max(0.0, eq.peakResponseDb()));
    return 0;
}
