// Checks the bundled headphone library loads and matches AutoEQ's own files.
// Usage: test_library <data/headphones.tsv> <presets/sample dir>
#include "../src/dsp/HeadphoneLibrary.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

static int failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } \
    } while (0)

static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: %s library.tsv sampledir\n", argv[0]); return 2; }
    const std::string sample = std::string(argv[2]) + "/Sennheiser HD 650 ";

    bteq::HeadphoneLibrary lib;
    CHECK(lib.loadFromFile(argv[1]));
    std::printf("AutoEQ %s: %zu profiles, %zu headphones\n", lib.autoEqVersion().c_str(),
                lib.numProfiles(), lib.numHeadphones());
    CHECK(lib.numHeadphones() > 5000);
    CHECK(lib.graphicFrequencies().size() == 127);

    // The oratory1990 HD 650 profile must equal AutoEQ's ParametricEQ.txt exactly.
    const auto* p = lib.find("Sennheiser HD 650", "oratory1990", false);
    CHECK(p != nullptr);
    auto ref = bteq::parseParametricText(readFile(sample + "ParametricEQ.txt"));
    CHECK(ref.has_value());
    if (p && ref) {
        CHECK(p->recommended);
        CHECK(p->form == "over-ear");
        CHECK(std::fabs(p->parametric.preampDb - ref->preampDb) < 1e-9);
        CHECK(p->parametric.filters.size() == ref->filters.size());
        for (size_t i = 0; i < ref->filters.size() && i < p->parametric.filters.size(); ++i) {
            const auto& a = p->parametric.filters[i];
            const auto& b = ref->filters[i];
            CHECK(a.type == b.type);
            CHECK(std::fabs(a.fc - b.fc) < 1e-9 && std::fabs(a.gainDb - b.gainDb) < 1e-9 &&
                  std::fabs(a.q - b.q) < 1e-9);
        }
        // ...and the graphic curve must equal its GraphicEQ.txt.
        auto g = bteq::parseGraphicEqText(readFile(sample + "GraphicEQ.txt"));
        CHECK(g.has_value());
        if (g) {
            CHECK(g->freqs == lib.graphicFrequencies());
            CHECK(p->graphicGainsDb.size() == g->gainsDb.size());
            for (size_t i = 0; i < g->gainsDb.size() && i < p->graphicGainsDb.size(); ++i)
                CHECK(std::fabs(p->graphicGainsDb[i] - g->gainsDb[i]) < 1e-6);
        }
    }

    // Search ignores case, spaces and punctuation, and lists the recommended source first.
    auto hits = lib.search("sennheiser hd650");
    bool found = false;
    for (auto i : hits)
        if (lib.headphoneName(i) == "Sennheiser HD 650") {
            found = true;
            auto profs = lib.profilesFor(i);
            CHECK(profs.size() >= 5);
            CHECK(profs.front()->originalPreset);
            std::printf("HD 650 profiles:\n");
            for (auto* pr : profs) std::printf("  %s\n", pr->label().c_str());
        }
    CHECK(found);
    CHECK(lib.search("zzzz-no-such-headphone").empty());

    // oratory1990's own HD 650 preset (Harman target) is carried over unprocessed,
    // exactly as published in his PDF (via OPRA).
    const auto* o = lib.find("Sennheiser HD 650", "oratory1990", true);
    CHECK(o != nullptr);
    if (o) {
        CHECK(o->target == "Harman Target");
        CHECK(std::fabs(o->parametric.preampDb - (-9.3)) < 1e-9);
        CHECK(o->parametric.filters.size() == 10);
        if (o->parametric.filters.size() == 10) {
            const auto& f0 = o->parametric.filters[0];
            CHECK(f0.type == bteq::FilterType::Peaking && f0.fc == 20 && f0.gainDb == 4 && f0.q == 1.1);
            const auto& f5 = o->parametric.filters[5];
            CHECK(f5.type == bteq::FilterType::HighShelf && f5.fc == 2000 && f5.gainDb == 3.5 && f5.q == 0.71);
        }
    }

    // Every profile must produce a stable, finite filter chain at common sample rates.
    size_t lowPass = 0;
    for (size_t h = 0; h < lib.numHeadphones(); ++h)
        for (const auto* pr : lib.profilesFor(h)) {
            if (!pr->hasParametric) continue;
            for (const auto& f : pr->parametric.filters) if (f.type == bteq::FilterType::LowPass) ++lowPass;
            for (double fs : {44100.0, 48000.0, 96000.0}) {
                bteq::ParametricEq eq;
                eq.prepare(pr->parametric, fs, 1);
                const double r = eq.responseDb(1000) + eq.responseDb(100) + eq.responseDb(10000);
                if (!std::isfinite(r) || !std::isfinite(eq.getPreampDb())) {
                    std::printf("bad response: %s / %s at %g\n", pr->name.c_str(), pr->label().c_str(), fs);
                    ++failures;
                }
            }
        }
    CHECK(lowPass >= 1);

    std::printf(failures ? "%d FAILURES\n" : "ALL PASSED\n", failures);
    return failures ? 1 : 0;
}
