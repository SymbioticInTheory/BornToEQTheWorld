#pragma once
// A chain of biquads plus preamp, loaded from an AutoEQ / Equalizer APO
// "ParametricEQ.txt" preset. No plugin-framework dependencies, so it can be
// unit-tested on its own and dropped into the JUCE plugin later.

#include "Biquad.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace bteq {

struct FilterSpec {
    FilterType type = FilterType::Peaking;
    double fc = 1000, gainDb = 0, q = 0.7071;
    bool enabled = true;
};

struct ParametricPreset {
    double preampDb = 0;
    std::vector<FilterSpec> filters;
};

// Parses lines such as:
//   Preamp: -6.1 dB
//   Filter 1: ON LSC Fc 105 Hz Gain 6.4 dB Q 0.70
// Unknown filter types are skipped (returned in `skipped`) rather than guessed at.
inline std::optional<ParametricPreset> parseParametricText(const std::string& text,
                                                           std::vector<std::string>* skipped = nullptr) {
    ParametricPreset p;
    std::istringstream in(text);
    std::string line;
    bool any = false;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string head;
        if (!(ls >> head)) continue;
        if (head == "Preamp:") {
            ls >> p.preampDb;
            any = true;
            continue;
        }
        if (head != "Filter" && head.rfind("Filter", 0) != 0) continue;
        // Skip the "1:" index token if present.
        std::string tok;
        if (head == "Filter") ls >> tok;
        std::string onoff, type;
        ls >> onoff >> type;
        FilterSpec f;
        f.enabled = (onoff == "ON");
        if (type == "PK" || type == "PEQ") f.type = FilterType::Peaking;
        else if (type == "LSC" || type == "LS") f.type = FilterType::LowShelf;
        else if (type == "HSC" || type == "HS") f.type = FilterType::HighShelf;
        else { if (skipped) skipped->push_back(line); continue; }
        std::string key;
        while (ls >> key) {
            if (key == "Fc") ls >> f.fc >> tok;          // value + "Hz"
            else if (key == "Gain") ls >> f.gainDb >> tok; // value + "dB"
            else if (key == "Q") ls >> f.q;
        }
        p.filters.push_back(f);
        any = true;
    }
    if (!any) return std::nullopt;
    return p;
}

inline std::optional<ParametricPreset> loadParametricFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return std::nullopt;
    std::stringstream ss;
    ss << f.rdbuf();
    return parseParametricText(ss.str());
}

class ParametricEq {
public:
    // amount: correction strength 0..1 (1 = full correction). Filter gains are
    // scaled by it. autoPreamp: when true, preamp is recomputed from the scaled
    // response so the peak boost is exactly cancelled; otherwise the file's
    // preamp is scaled by `amount`.
    void prepare(const ParametricPreset& preset, double sampleRate, int numChannels,
                 double amount = 1.0, bool autoPreamp = true) {
        fs = sampleRate;
        coeffs.clear();
        for (const auto& f : preset.filters) {
            if (!f.enabled) continue;
            coeffs.push_back(BiquadCoeffs::make(f.type, f.fc, f.gainDb * amount, f.q, fs));
        }
        const double pre = autoPreamp ? -std::max(0.0, peakResponseDb()) : preset.preampDb * amount;
        preampDb = pre;
        preampGain = std::pow(10.0, pre / 20.0);
        channels.assign(numChannels, std::vector<Biquad>(coeffs.size()));
        for (auto& ch : channels)
            for (size_t i = 0; i < coeffs.size(); ++i) ch[i].setCoeffs(coeffs[i]);
    }

    void reset() {
        for (auto& ch : channels)
            for (auto& b : ch) b.reset();
    }

    template <typename Sample>
    void processChannel(int channel, Sample* data, int numSamples) {
        auto& chain = channels[channel];
        for (int n = 0; n < numSamples; ++n) {
            double x = static_cast<double>(data[n]) * preampGain;
            for (auto& b : chain) x = b.process(x);
            data[n] = static_cast<Sample>(x);
        }
    }

    // Total response (filters only, no preamp) in dB.
    double responseDb(double f) const {
        double db = 0;
        for (const auto& c : coeffs) db += c.magnitudeDb(f, fs);
        return db;
    }

    // Highest point of the filter response between 20 Hz and 20 kHz.
    double peakResponseDb() const {
        double peak = -1e9;
        for (int i = 0; i <= 2000; ++i) {
            const double f = 20.0 * std::pow(1000.0, i / 2000.0);
            if (f >= fs / 2) break;
            peak = std::max(peak, responseDb(f));
        }
        return peak;
    }

    double getPreampDb() const { return preampDb; }

private:
    double fs = 48000;
    double preampDb = 0, preampGain = 1;
    std::vector<BiquadCoeffs> coeffs;
    std::vector<std::vector<Biquad>> channels;
};

} // namespace bteq
