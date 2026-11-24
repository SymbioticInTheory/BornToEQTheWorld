#pragma once
// Biquad filter matching Equalizer APO's approach: RBJ "Audio EQ Cookbook"
// coefficients, computed and run in double precision at the host sample rate.

#include <cmath>

namespace bteq {

constexpr double kPi = 3.14159265358979323846;

enum class FilterType { Peaking, LowShelf, HighShelf };

struct BiquadCoeffs {
    // Normalised so a0 == 1. Difference equation:
    // y[n] = b0 x[n] + b1 x[n-1] + b2 x[n-2] - a1 y[n-1] - a2 y[n-2]
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    static BiquadCoeffs identity() { return {}; }

    // fc in Hz, gainDb in dB, q is the cookbook Q (as written in AutoEQ/APO files).
    static BiquadCoeffs make(FilterType type, double fc, double gainDb, double q, double fs) {
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * kPi * fc / fs;
        const double cw = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * q);
        double b0, b1, b2, a0, a1, a2;

        switch (type) {
        case FilterType::Peaking:
            b0 = 1 + alpha * A;
            b1 = -2 * cw;
            b2 = 1 - alpha * A;
            a0 = 1 + alpha / A;
            a1 = -2 * cw;
            a2 = 1 - alpha / A;
            break;
        case FilterType::LowShelf: {
            const double s = 2 * std::sqrt(A) * alpha;
            b0 = A * ((A + 1) - (A - 1) * cw + s);
            b1 = 2 * A * ((A - 1) - (A + 1) * cw);
            b2 = A * ((A + 1) - (A - 1) * cw - s);
            a0 = (A + 1) + (A - 1) * cw + s;
            a1 = -2 * ((A - 1) + (A + 1) * cw);
            a2 = (A + 1) + (A - 1) * cw - s;
            break;
        }
        case FilterType::HighShelf:
        default: {
            const double s = 2 * std::sqrt(A) * alpha;
            b0 = A * ((A + 1) + (A - 1) * cw + s);
            b1 = -2 * A * ((A - 1) + (A + 1) * cw);
            b2 = A * ((A + 1) + (A - 1) * cw - s);
            a0 = (A + 1) - (A - 1) * cw + s;
            a1 = 2 * ((A - 1) - (A + 1) * cw);
            a2 = (A + 1) - (A - 1) * cw - s;
            break;
        }
        }
        return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    }

    // Magnitude response in dB at frequency f (for drawing the curve and tests).
    double magnitudeDb(double f, double fs) const {
        const double w = 2.0 * kPi * f / fs;
        const double c1 = std::cos(w), s1 = std::sin(w);
        const double c2 = std::cos(2 * w), s2 = std::sin(2 * w);
        const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
        const double dr = 1 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        return 10.0 * std::log10((nr * nr + ni * ni) / (dr * dr + di * di));
    }
};

// Direct Form I with double precision state, the same structure Equalizer APO uses.
class Biquad {
public:
    void setCoeffs(const BiquadCoeffs& c) { k = c; }
    void reset() { x1 = x2 = y1 = y2 = 0; }

    inline double process(double x) {
        const double y = k.b0 * x + k.b1 * x1 + k.b2 * x2 - k.a1 * y1 - k.a2 * y2;
        x2 = x1; x1 = x;
        y2 = y1;
        // Flush denormals so quiet passages don't spike the CPU.
        y1 = (std::fabs(y) < 1e-30) ? 0.0 : y;
        return y;
    }

private:
    BiquadCoeffs k;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
};

} // namespace bteq
