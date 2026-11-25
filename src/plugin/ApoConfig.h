#pragma once

// Text helpers for sending BEQ's EQ to Equalizer APO. Plain C++ so they can
// be tested without JUCE. Equalizer APO reads config.txt from its config
// folder and reloads it whenever a file there changes.

#include "../dsp/ParametricEq.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace bteq::apo {

inline const std::string kFileName = "BornToEQTheWorld.txt";
inline const std::string kInclude = "Include: " + kFileName;
// Lines of config.txt that BEQ switched off are commented out with this mark,
// so they can be switched back on exactly as they were.
inline const std::string kMark = "# [BEQ] ";
inline const std::string kHeader =
    "# Born To EQ The World sends its EQ through the file below. Lines marked [BEQ] were switched off by it "
    "and come back when you turn Equalizer APO off in BEQ.";

// Number with up to `decimals` places, without trailing zeros.
inline std::string num(double v, int decimals) {
    if (std::abs(v) < 0.5 * std::pow(10.0, -decimals)) v = 0.0;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
}

// One filter in Equalizer APO's syntax, with its gain scaled (the correction amount).
// Shelves use the Q form (LSC/HSC ... Q), which APO computes like BEQ does.
inline std::string filterLine(const FilterSpec& f, double gainScale) {
    const std::string fc = num(f.fc, 2), q = num(f.q, 4), gain = num(f.gainDb * gainScale, 3);
    switch (f.type) {
    case FilterType::LowShelf: return "Filter: ON LSC Fc " + fc + " Hz Gain " + gain + " dB Q " + q;
    case FilterType::HighShelf: return "Filter: ON HSC Fc " + fc + " Hz Gain " + gain + " dB Q " + q;
    case FilterType::LowPass: return "Filter: ON LPQ Fc " + fc + " Hz Q " + q;
    case FilterType::Peaking: break;
    }
    return "Filter: ON PK Fc " + fc + " Hz Gain " + gain + " dB Q " + q;
}

inline std::string graphicLine(const std::vector<double>& freqs, const std::vector<double>& gainsDb) {
    std::string s = "GraphicEQ: ";
    for (size_t i = 0; i < freqs.size() && i < gainsDb.size(); ++i) {
        if (i > 0) s += "; ";
        s += num(freqs[i], 2) + " " + num(gainsDb[i], 3);
    }
    return s;
}

inline std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string cur;
    for (const char c : text) {
        if (c == '\n') {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            lines.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

inline bool isOurLine(const std::string& line) {
    std::string t = trim(line);
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    std::string inc = kInclude, head = kHeader;
    std::transform(inc.begin(), inc.end(), inc.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    std::transform(head.begin(), head.end(), head.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return t == inc || t == head;
}

inline std::string joinLines(const std::vector<std::string>& lines) {
    std::string s;
    for (const auto& l : lines) s += l + "\r\n";
    return s;
}

// config.txt with BEQ's file included and every other active line (Peace's
// include, filters, device selections) commented out, so only BEQ's EQ applies.
// Running it again changes nothing.
inline std::string takeOver(const std::string& config) {
    std::vector<std::string> out { kHeader, kInclude };
    for (const auto& line : splitLines(config)) {
        if (isOurLine(line)) continue;
        const auto t = trim(line);
        out.push_back(t.empty() || t[0] == '#' ? line : kMark + line);
    }
    return joinLines(out);
}

// config.txt as it was before takeOver().
inline std::string restore(const std::string& config) {
    std::vector<std::string> out;
    for (const auto& line : splitLines(config)) {
        if (isOurLine(line)) continue;
        out.push_back(line.rfind(kMark, 0) == 0 ? line.substr(kMark.size()) : line);
    }
    return joinLines(out);
}

inline bool isTakenOver(const std::string& config) {
    for (const auto& line : splitLines(config))
        if (isOurLine(line) && trim(line)[0] != '#') return true;
    return false;
}

} // namespace bteq::apo
