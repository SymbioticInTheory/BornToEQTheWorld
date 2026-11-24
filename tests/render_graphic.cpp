// Writes the GraphicEQ FIR for a GraphicEQ.txt file as raw float32 taps.
// Usage: render_graphic <GraphicEQ.txt> <sampleRate> <amount> <out.raw>
#include "../src/dsp/GraphicEq.h"
#include "../src/dsp/HeadphoneLibrary.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

int main(int argc, char** argv) {
    if (argc < 5) { std::fprintf(stderr, "usage\n"); return 2; }
    std::ifstream f(argv[1]);
    std::stringstream ss; ss << f.rdbuf();
    auto c = bteq::parseGraphicEqText(ss.str());
    if (!c) return 1;
    auto gains = bteq::scaleGraphicGains(c->gainsDb, std::atof(argv[3]));
    auto taps = bteq::designGraphicEqFir(c->freqs, gains, std::atof(argv[2]));
    FILE* o = std::fopen(argv[4], "wb");
    std::fwrite(taps.data(), sizeof(float), taps.size(), o);
    std::fclose(o);
    return 0;
}
