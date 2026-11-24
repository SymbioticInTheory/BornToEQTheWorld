#pragma once

// Measurements and target curves for the advanced graph (data/curves.tsv, made
// by tools/build_curves.py). All curves are on the headphone library's 127
// GraphicEQ frequencies, in dB, 0 dB at 1 kHz.

#include <cstdlib>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace bteq {

class CurveLibrary {
public:
    struct Curves {
        const std::vector<float>* measured = nullptr; // nullptr when there is no raw measurement
        const std::vector<float>* target = nullptr;   // nullptr when the target is unknown
    };

    struct Measurement {
        std::string source, rig;
        const std::vector<float>* curve = nullptr;
    };

    bool loadFromText(const std::string& text) {
        targets.clear();
        measurements.clear();
        profiles.clear();
        byName.clear();
        std::istringstream in(text);
        std::string line;
        if (!std::getline(in, line) || line.rfind("BTEQCURVES\t", 0) != 0) return false;
        while (std::getline(in, line)) {
            if (line.size() < 2) continue;
            const auto cols = split(line, '\t');
            if (cols[0] == "T" && cols.size() >= 3) targets[cols[1]] = parseValues(cols[2]);
            else if (cols[0] == "M" && cols.size() >= 3) measurements[cols[1]] = parseValues(cols[2]);
            else if (cols[0] == "P" && cols.size() >= 8) {
                profiles[key(cols[1], cols[2], cols[3], cols[4] == "O", cols[5])] = { cols[6], cols[7] };
                if (cols[6] != "-") {
                    auto& list = byName[cols[1]];
                    bool seen = false;
                    for (const auto& m : list) seen = seen || m.id == cols[6];
                    if (!seen) list.push_back({ cols[2], cols[3], cols[6] });
                }
            }
        }
        return !profiles.empty();
    }

    // Every measurement of a headphone, in library order (AutoEQ's recommended source first).
    std::vector<Measurement> measurementsOf(const std::string& name) const {
        std::vector<Measurement> out;
        const auto it = byName.find(name);
        if (it == byName.end()) return out;
        for (const auto& m : it->second)
            if (const auto c = measurements.find(m.id); c != measurements.end()) out.push_back({ m.source, m.rig, &c->second });
        return out;
    }

    bool hasMeasurement(const std::string& name) const { return byName.count(name) > 0; }

    Curves find(const std::string& name, const std::string& source, const std::string& rig, bool originalPreset,
                const std::string& target) const {
        Curves c;
        const auto it = profiles.find(key(name, source, rig, originalPreset, target));
        if (it == profiles.end()) return c;
        if (const auto m = measurements.find(it->second.first); m != measurements.end()) c.measured = &m->second;
        if (const auto t = targets.find(it->second.second); t != targets.end()) c.target = &t->second;
        return c;
    }

private:
    static std::string key(const std::string& name, const std::string& source, const std::string& rig, bool o,
                           const std::string& target) {
        return name + '\t' + source + '\t' + rig + '\t' + (o ? "O" : "A") + '\t' + target;
    }

    static std::vector<std::string> split(const std::string& s, char sep) {
        std::vector<std::string> out;
        size_t start = 0;
        for (size_t i = 0; i <= s.size(); ++i)
            if (i == s.size() || s[i] == sep) {
                out.push_back(s.substr(start, i - start));
                start = i + 1;
            }
        return out;
    }

    // Comma separated tenths of a dB.
    static std::vector<float> parseValues(const std::string& s) {
        std::vector<float> v;
        const char* p = s.c_str();
        while (*p != '\0') {
            char* end = nullptr;
            const long x = std::strtol(p, &end, 10);
            if (end == p) break;
            v.push_back(static_cast<float>(x) / 10.0f);
            p = *end == ',' ? end + 1 : end;
        }
        return v;
    }

    std::unordered_map<std::string, std::vector<float>> targets, measurements;
    std::unordered_map<std::string, std::pair<std::string, std::string>> profiles;
    struct MeasurementRef { std::string source, rig, id; };
    std::unordered_map<std::string, std::vector<MeasurementRef>> byName;
};

} // namespace bteq
