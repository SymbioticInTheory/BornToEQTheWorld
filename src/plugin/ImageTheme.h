#pragma once

// Picks BEQ's colours from a background image: an accent that stands out on
// the dark panels, a fog tint for the haze over the image, and curve colours
// for the advanced view that stay clear of the accent. Plain C++ so it can be
// tested without JUCE. chrome-extension/theme.js does the same in JavaScript;
// keep the two in step.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace bteq::imagetheme {

struct Rgb { double r = 0, g = 0, b = 0; }; // 0..1

struct Colours {
    Rgb accent;            // swatch, sliders, curve, title
    Rgb fog;               // tint of the haze over the image (very dark)
    Rgb measured, eq;      // advanced view curves
    bool vivid = true;     // false: the image is nearly grey, so the accent is a light neutral
};

inline double clamp01(double v) { return std::min(1.0, std::max(0.0, v)); }

inline void toHsv(const Rgb& c, double& h, double& s, double& v) {
    const double mx = std::max({ c.r, c.g, c.b }), mn = std::min({ c.r, c.g, c.b }), d = mx - mn;
    v = mx;
    s = mx > 0 ? d / mx : 0;
    if (d <= 0) { h = 0; return; }
    if (mx == c.r) h = std::fmod((c.g - c.b) / d + 6.0, 6.0);
    else if (mx == c.g) h = (c.b - c.r) / d + 2.0;
    else h = (c.r - c.g) / d + 4.0;
    h *= 60.0;
}

// HSL with hue in degrees, s and l in 0..1.
inline Rgb fromHsl(double h, double s, double l) {
    h = std::fmod(std::fmod(h, 360.0) + 360.0, 360.0);
    const double c = (1 - std::abs(2 * l - 1)) * s, x = c * (1 - std::abs(std::fmod(h / 60.0, 2.0) - 1)), m = l - c / 2;
    double r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else { r = c; b = x; }
    return { clamp01(r + m), clamp01(g + m), clamp01(b + m) };
}

inline void toHsl(const Rgb& c, double& h, double& s, double& l) {
    double v, sv;
    toHsv(c, h, sv, v);
    const double mx = std::max({ c.r, c.g, c.b }), mn = std::min({ c.r, c.g, c.b });
    l = (mx + mn) / 2;
    s = (mx == mn) ? 0 : (mx - mn) / (1 - std::abs(2 * l - 1));
}

inline double luminance(const Rgb& c) {
    auto lin = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b);
}

inline double hueDistance(double a, double b) {
    const double d = std::abs(std::fmod(a - b + 540.0, 360.0) - 180.0);
    return d;
}

// The first candidate (in order of preference) whose hue is at least `enough` degrees
// from every hue in `avoid`; if none is, the one that gets furthest.
inline Rgb furthest(const std::vector<Rgb>& candidates, const std::vector<double>& avoid, double enough = 75.0) {
    Rgb best = candidates.front();
    double bestScore = -1;
    for (const auto& c : candidates) {
        double h, s, v;
        toHsv(c, h, s, v);
        double score = 360;
        for (double a : avoid) score = std::min(score, hueDistance(h, a));
        if (score >= enough) return c;
        if (score > bestScore + 1e-9) { bestScore = score; best = c; }
    }
    return best;
}

inline Rgb hex(uint32_t v) { return { ((v >> 16) & 255) / 255.0, ((v >> 8) & 255) / 255.0, (v & 255) / 255.0 }; }

// `rgb` holds 8-bit R, G, B triplets (an image scaled down to ~96 px is plenty).
inline Colours pick(const std::vector<uint8_t>& rgb) {
    constexpr int bins = 36;
    double hist[bins] = {}, cosSum[bins] = {}, sinSum[bins] = {}, satSum[bins] = {};
    double vividTotal = 0, darkR = 0, darkG = 0, darkB = 0, darkN = 0, allR = 0, allG = 0, allB = 0;
    const size_t n = rgb.size() / 3;
    for (size_t i = 0; i < n; ++i) {
        const Rgb c { rgb[i * 3] / 255.0, rgb[i * 3 + 1] / 255.0, rgb[i * 3 + 2] / 255.0 };
        double h, s, v;
        toHsv(c, h, s, v);
        allR += c.r; allG += c.g; allB += c.b;
        if (v < 0.4) { darkR += c.r; darkG += c.g; darkB += c.b; darkN += 1; }
        if (s < 0.2 || v < 0.25) continue;
        const double w = s * s * v * v; // favours strong, bright colour over large dull areas
        const int b = std::min(bins - 1, (int)(h / (360.0 / bins)));
        const double rad = h * 3.14159265358979323846 / 180.0;
        hist[b] += w;
        cosSum[b] += std::cos(rad) * w;
        sinSum[b] += std::sin(rad) * w;
        satSum[b] += s * w;
        vividTotal += w;
    }

    Colours out;
    // Fog: the hue of the image's dark areas, kept nearly black.
    Rgb dark = darkN > 0 ? Rgb { darkR / darkN, darkG / darkN, darkB / darkN }
                         : (n > 0 ? Rgb { allR / n, allG / n, allB / n } : Rgb {});
    {
        double h, s, l;
        toHsl(dark, h, s, l);
        out.fog = fromHsl(h, std::min(s, 0.35), 0.045);
    }

    double accentHue;
    if (n == 0 || vividTotal / (double)n < 0.003) {
        // Nearly grey image: a light, barely tinted neutral (like Bone).
        out.vivid = false;
        double h, s, l;
        toHsl(n > 0 ? Rgb { allR / n, allG / n, allB / n } : Rgb {}, h, s, l);
        accentHue = h;
        out.accent = fromHsl(h, std::min(s, 0.10), 0.84);
    } else {
        int peak = 0;
        double best = -1;
        for (int b = 0; b < bins; ++b) {
            const double score = hist[b] + 0.5 * (hist[(b + bins - 1) % bins] + hist[(b + 1) % bins]);
            if (score > best) { best = score; peak = b; }
        }
        double cs = 0, sn = 0, sat = 0, w = 0;
        for (int d = -1; d <= 1; ++d) {
            const int b = (peak + d + bins) % bins;
            cs += cosSum[b]; sn += sinSum[b]; sat += satSum[b]; w += hist[b];
        }
        accentHue = std::fmod(std::atan2(sn, cs) * 180.0 / 3.14159265358979323846 + 360.0, 360.0);
        const double s = std::clamp(sat / w, 0.5, 0.85);
        // Light enough to stand out on near-black panels.
        double l = 0.52;
        out.accent = fromHsl(accentHue, s, l);
        while (luminance(out.accent) < 0.16 && l < 0.75) {
            l += 0.01;
            out.accent = fromHsl(accentHue, s, l);
        }
    }

    // Advanced view: EQ and measured curves clear of the accent and of the gold target curve.
    const double gold = 43.0;
    std::vector<double> avoid { gold };
    if (out.vivid) avoid.push_back(accentHue);
    out.eq = furthest({ hex(0x3CBFA4), hex(0xD2607E), hex(0xB4A6EC) }, avoid);
    double eqHue, s, v;
    toHsv(out.eq, eqHue, s, v);
    avoid.push_back(eqHue);
    out.measured = out.vivid ? furthest({ hex(0x6F8296), hex(0x4F86D6), hex(0x9A9EA4) }, avoid, 40.0) : hex(0x4F86D6);
    return out;
}

} // namespace bteq::imagetheme
