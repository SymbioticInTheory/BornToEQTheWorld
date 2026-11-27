#pragma once
// Headphone profile library built from AutoEQ by tools/build_library.py.
// Pick a headphone by name, then one of its profiles (measurement source).
// No plugin-framework dependencies.

#include "ParametricEq.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace bteq {

struct HeadphoneProfile {
    std::string name;    // e.g. "Sennheiser HD 650"
    std::string source;  // e.g. "oratory1990", "crinacle"
    std::string rig;     // measurement rig, e.g. "GRAS 43AG-7" (often empty)
    std::string form;    // "over-ear", "in-ear" or "earbud"
    bool recommended = false; // AutoEQ's pick of the best source for this headphone
    // True when this is the source's own preset used exactly as published
    // (oratory1990's hand-made EQs); false when AutoEQ computed it from a measurement.
    bool originalPreset = false;
    std::string target; // e.g. "Harman Target", "Harman over-ear 2018", "oratory1990 Optimum HiFi"
    bool hasParametric = false;
    ParametricPreset parametric;
    // Graphic EQ gains, matching HeadphoneLibrary::graphicFrequencies(). Kept as
    // tenths of a dB (every published value has one decimal), which takes half the
    // memory of floats and gives back exactly the same numbers.
    std::vector<int16_t> graphicTenths;
    bool hasGraphic() const { return !graphicTenths.empty(); }
    size_t graphicSize() const { return graphicTenths.size(); }
    float graphicDb(size_t i) const { return static_cast<float>(graphicTenths[i] / 10.0); }
    std::vector<float> graphicGainsDb() const {
        std::vector<float> out(graphicTenths.size());
        for (size_t i = 0; i < out.size(); ++i) out[i] = graphicDb(i);
        return out;
    }

    // Label for the source dropdown, e.g. "crinacle (GRAS 43AG-7)".
    std::string sourceLabel() const { return rig.empty() ? source : source + " (" + rig + ")"; }

    // Full label, e.g. "oratory1990 preset: Harman Target" or
    // "Rtings (HMS II.3) via AutoEQ: Harman over-ear 2018".
    std::string label() const {
        return sourceLabel() + (originalPreset ? " preset: " : " via AutoEQ: ") + target;
    }
};

struct GraphicCurve {
    std::vector<double> freqs, gainsDb;
};

// Parses a Wavelet / Equalizer APO "GraphicEQ: 20 -0.2; 21 -0.2; ..." line.
inline std::optional<GraphicCurve> parseGraphicEqText(const std::string& text) {
    const auto colon = text.find("GraphicEQ:");
    if (colon == std::string::npos) return std::nullopt;
    GraphicCurve c;
    std::istringstream in(text.substr(colon + 10));
    std::string point;
    while (std::getline(in, point, ';')) {
        std::istringstream ps(point);
        double f, g;
        if (ps >> f >> g) { c.freqs.push_back(f); c.gainsDb.push_back(g); }
    }
    if (c.freqs.empty()) return std::nullopt;
    return c;
}

namespace detail {
inline std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        if (ch == sep) { out.push_back(cur); cur.clear(); }
        else cur += ch;
    }
    out.push_back(cur);
    return out;
}

// Lowercase letters and digits only, so "hd650" matches "HD 650" and "HD-650".
inline std::string searchKey(const std::string& s) {
    std::string k;
    for (unsigned char ch : s)
        if (std::isalnum(ch)) k += static_cast<char>(std::tolower(ch));
        else if (ch >= 0x80) k += static_cast<char>(ch); // keep non-ASCII as-is
    return k;
}
} // namespace detail

class HeadphoneLibrary {
public:
    // Parses the contents of data/headphones.tsv. Returns false on a bad header.
    bool loadFromText(const std::string& text) { return loadFromText(text.data(), text.size()); }

    // Reads straight from the buffer, one line at a time, so loading needs no extra
    // copies of the (22 MB) text.
    bool loadFromText(const char* data, size_t size) {
        profiles.clear(); names.clear(); freqs.clear(); version.clear();
        size_t pos = 0;
        std::string line;
        auto getline = [&](std::string& out) {
            if (pos >= size) return false;
            const char* start = data + pos;
            const void* nl = std::memchr(start, '\n', size - pos);
            const size_t len = nl != nullptr ? (size_t)((const char*)nl - start) : size - pos;
            out.assign(start, len);
            pos += len + 1;
            return true;
        };
        if (!getline(line)) return false;
        auto head = detail::split(line, '\t');
        if (head.size() < 3 || head[0] != "BTEQLIB" || head[1] != "2") return false;
        version = head[2];
        if (!getline(line)) return false;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto fr = detail::split(line, '\t');
        if (fr.size() != 2 || fr[0] != "FREQS") return false;
        for (const auto& f : detail::split(fr[1], ',')) freqs.push_back(std::atof(f.c_str()));

        while (getline(line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            auto c = detail::split(line, '\t');
            if (c.size() != 10) continue;
            HeadphoneProfile p;
            p.name = c[0]; p.source = c[1]; p.rig = c[2]; p.form = c[3];
            p.recommended = c[4] == "1";
            p.originalPreset = c[5] == "O";
            p.target = c[6];
            p.parametric.preampDb = std::atof(c[7].c_str());
            if (!c[8].empty()) {
                p.hasParametric = true;
                for (const auto& fs : detail::split(c[8], ';')) {
                    auto v = detail::split(fs, ',');
                    if (v.size() != 4 || v[0].empty()) continue;
                    FilterSpec f;
                    switch (v[0][0]) {
                    case 'L': f.type = FilterType::LowShelf; break;
                    case 'H': f.type = FilterType::HighShelf; break;
                    case 'W': f.type = FilterType::LowPass; break;
                    default: f.type = FilterType::Peaking; break;
                    }
                    f.fc = std::atof(v[1].c_str());
                    f.gainDb = std::atof(v[2].c_str());
                    f.q = std::atof(v[3].c_str());
                    p.parametric.filters.push_back(f);
                }
            }
            if (!c[9].empty())
                for (const auto& g : detail::split(c[9], ','))
                    p.graphicTenths.push_back(static_cast<int16_t>(std::lround(std::atof(g.c_str()) * 10.0)));
            p.graphicTenths.shrink_to_fit();
            p.parametric.filters.shrink_to_fit();
            if (names.empty() || names.back().name != p.name)
                names.push_back({p.name, detail::searchKey(p.name), profiles.size(), 0});
            names.back().count++;
            profiles.push_back(std::move(p));
        }
        profiles.shrink_to_fit();
        names.shrink_to_fit();
        return !profiles.empty();
    }

    bool loadFromFile(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        std::stringstream ss;
        ss << f.rdbuf();
        return loadFromText(ss.str());
    }

    size_t numProfiles() const { return profiles.size(); }
    size_t numHeadphones() const { return names.size(); }
    const std::string& autoEqVersion() const { return version; }
    const std::vector<double>& graphicFrequencies() const { return freqs; }
    const std::string& headphoneName(size_t i) const { return names[i].name; }

    // Headphone indices whose name contains every word of the query
    // (case, spaces and punctuation ignored). Empty query returns all.
    std::vector<size_t> search(const std::string& query, size_t limit = SIZE_MAX) const {
        std::vector<std::string> words;
        std::istringstream in(query);
        for (std::string w; in >> w;) {
            auto k = detail::searchKey(w);
            if (!k.empty()) words.push_back(k);
        }
        std::vector<size_t> out;
        for (size_t i = 0; i < names.size() && out.size() < limit; ++i) {
            bool ok = true;
            for (const auto& w : words)
                if (names[i].key.find(w) == std::string::npos) { ok = false; break; }
            if (ok) out.push_back(i);
        }
        return out;
    }

    // Profiles for one headphone: oratory1990's own presets first, then AutoEQ's
    // recommended source, then the other sources.
    std::vector<const HeadphoneProfile*> profilesFor(size_t index) const {
        std::vector<const HeadphoneProfile*> out;
        const auto& n = names[index];
        for (size_t i = 0; i < n.count; ++i) out.push_back(&profiles[n.first + i]);
        return out;
    }

    // Index of the headphone with exactly this name.
    std::optional<size_t> headphoneIndex(const std::string& name) const {
        for (size_t i = 0; i < names.size(); ++i)
            if (names[i].name == name) return i;
        return std::nullopt;
    }

    // Exact-name lookup, optionally narrowed to one source and to AutoEQ results
    // (originalPreset = false) or the source's own presets (true).
    const HeadphoneProfile* find(const std::string& name, const std::string& source = {},
                                 std::optional<bool> originalPreset = std::nullopt) const {
        for (const auto& p : profiles)
            if (p.name == name && (source.empty() || p.source == source) &&
                (!originalPreset || p.originalPreset == *originalPreset))
                return &p;
        return nullptr;
    }

private:
    struct Name { std::string name, key; size_t first, count; };
    std::vector<HeadphoneProfile> profiles;
    std::vector<Name> names;
    std::vector<double> freqs;
    std::string version;
};

} // namespace bteq
