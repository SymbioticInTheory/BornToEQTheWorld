#pragma once
// GraphicEQ (Wavelet / Equalizer APO "GraphicEQ:" curves) as a minimum-phase FIR.
//
// Designed the same way Equalizer APO's GraphicEQ filter does it, so the result
// sounds the same as Peace:
//   * gains are interpolated linearly in dB over log frequency between the
//     points, and held flat beyond the first and last point;
//   * the magnitude is sampled on a 2 * 16384 point FFT grid at the host rate;
//   * a minimum-phase response is derived from it with the real cepstrum;
//   * the impulse response is faded with the falling half of a Hann window
//     and cut to 16384 taps.
// Independent implementation (own FFT); no plugin-framework dependencies.

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace bteq {

namespace detail {
// In-place iterative radix-2 FFT. inverse = true computes the unscaled inverse.
inline void fft(std::vector<std::complex<double>>& a, bool inverse) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2 * 3.14159265358979323846 / double(len) * (inverse ? 1 : -1);
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
} // namespace detail

// Gain in dB at `freq`, interpolated linearly over log frequency between the
// points (freqs ascending), held flat outside them.
inline double graphicGainAt(const std::vector<double>& freqs, const std::vector<double>& gainsDb, double freq) {
    if (freqs.empty()) return 0.0;
    if (freq <= freqs.front()) return gainsDb.front();
    if (freq >= freqs.back()) return gainsDb.back();
    size_t hi = 1;
    while (freqs[hi] < freq) ++hi;
    const size_t lo = hi - 1;
    if (gainsDb[lo] == gainsDb[hi]) return gainsDb[lo];
    const double t = (std::log(freq) - std::log(freqs[lo])) / (std::log(freqs[hi]) - std::log(freqs[lo]));
    return gainsDb[lo] + t * (gainsDb[hi] - gainsDb[lo]);
}

// Builds the minimum-phase FIR (`length` taps, a power of two).
inline std::vector<float> designGraphicEqFir(const std::vector<double>& freqs, const std::vector<double>& gainsDb,
                                             double sampleRate, size_t length = 16384) {
    using C = std::complex<double>;
    const size_t n = length * 2;
    std::vector<C> spec(n);
    for (size_t i = 0; i < length; ++i) {
        const double f = double(i) * sampleRate / double(n);
        const double g = std::pow(10.0, graphicGainAt(freqs, gainsDb, f) / 20.0);
        spec[i] = g;
        spec[n - i - 1] = g; // mirror the way Equalizer APO fills its grid
    }

    // Real cepstrum of the log magnitude.
    const double threshold = std::pow(10.0, -100.0 / 20.0);
    for (auto& v : spec) v = v.real() < threshold ? std::log(threshold) : std::log(v.real());
    detail::fft(spec, true);
    for (auto& v : spec) v /= double(n);

    // Fold the anti-causal part onto the causal part -> minimum phase.
    for (size_t i = 1; i < length; ++i) {
        spec[i] = C(spec[i].real() + spec[n - i].real(), spec[i].imag() - spec[n - i].imag());
        spec[n - i] = 0;
    }
    spec[length] = C(spec[length].real(), -spec[length].imag());

    detail::fft(spec, false);
    for (auto& v : spec) v = std::exp(v.real()) * C(std::cos(v.imag()), std::sin(v.imag()));

    detail::fft(spec, true);
    std::vector<float> taps(length);
    for (size_t i = 0; i < length; ++i) {
        const double window = 0.5 * (1.0 + std::cos(2.0 * 3.14159265358979323846 * double(i) / double(n)));
        taps[i] = static_cast<float>(spec[i].real() / double(n) * window);
    }
    return taps;
}

// Scales a GraphicEQ curve for the correction amount (0..1): the curve's shape
// is scaled around its highest point, so 100% is exactly the published curve,
// 0% is flat, and nothing in between ever boosts above the published peak.
inline std::vector<double> scaleGraphicGains(const std::vector<double>& gainsDb, double amount) {
    if (gainsDb.empty()) return {};
    double peak = gainsDb.front();
    for (double g : gainsDb) peak = std::max(peak, g);
    std::vector<double> out(gainsDb.size());
    for (size_t i = 0; i < gainsDb.size(); ++i) out[i] = peak + amount * (gainsDb[i] - peak);
    return out;
}

} // namespace bteq
