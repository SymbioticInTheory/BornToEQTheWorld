"""Independent check of the C++ DSP core against AutoEQ's own filter maths.

The reference below is transcribed from autoeq/peq.py (Peaking, LowShelf,
HighShelf .biquad_coefficients) and evaluated with scipy.signal.freqz. The C++
side is a real impulse rendered sample-by-sample, so this checks both the
coefficients and the processing loop.
"""
import subprocess, sys, tempfile, os
import numpy as np
from scipy.signal import freqz

def autoeq_coeffs(kind, fc, gain, q, fs):
    a = 10 ** (gain / 40); w0 = 2 * np.pi * fc / fs; alpha = np.sin(w0) / (2 * q); c = np.cos(w0)
    if kind == 'PK':
        a0 = 1 + alpha / a; A = [1, -2 * c / a0, (1 - alpha / a) / a0]
        B = [(1 + alpha * a) / a0, -2 * c / a0, (1 - alpha * a) / a0]
    elif kind == 'LSC':
        s = 2 * np.sqrt(a) * alpha; a0 = (a + 1) + (a - 1) * c + s
        A = [1, -2 * ((a - 1) + (a + 1) * c) / a0, ((a + 1) + (a - 1) * c - s) / a0]
        B = [a * ((a + 1) - (a - 1) * c + s) / a0, 2 * a * ((a - 1) - (a + 1) * c) / a0, a * ((a + 1) - (a - 1) * c - s) / a0]
    else:
        s = 2 * np.sqrt(a) * alpha; a0 = (a + 1) - (a - 1) * c + s
        A = [1, 2 * ((a - 1) - (a + 1) * c) / a0, ((a + 1) - (a - 1) * c - s) / a0]
        B = [a * ((a + 1) + (a - 1) * c + s) / a0, -2 * a * ((a - 1) + (a + 1) * c) / a0, a * ((a + 1) + (a - 1) * c - s) / a0]
    return B, A

def parse(path):
    pre, fl = 0.0, []
    for line in open(path):
        t = line.split()
        if t and t[0] == 'Preamp:': pre = float(t[1])
        elif t and t[0] == 'Filter' and t[2] == 'ON':
            fl.append((t[3], float(t[5]), float(t[8]), float(t[11])))
    return pre, fl

def main(binary, preset):
    pre, filters = parse(preset)
    worst = 0.0
    for fs in (44100, 48000, 88200, 96000, 192000):
        for amount in (1.0, 0.5):
            with tempfile.TemporaryDirectory() as d:
                out = os.path.join(d, 'ir.raw')
                subprocess.run([binary, preset, str(fs), str(amount), out], check=True, capture_output=True)
                ir = np.fromfile(out, dtype=np.float64)
            freqs = np.geomspace(20, min(20000, fs / 2 * 0.99), 500)
            ref = np.full(freqs.shape, pre * amount)
            for k, fc, g, q in filters:
                B, A = autoeq_coeffs(k, fc, g * amount, q, fs)
                _, h = freqz(B, A, worN=freqs, fs=fs)
                ref += 20 * np.log10(np.abs(h))
            _, h = freqz(ir, [1], worN=freqs, fs=fs)
            got = 20 * np.log10(np.abs(h))
            err = np.max(np.abs(got - ref)); worst = max(worst, err)
            print(f'fs={fs:>6} amount={amount:.1f}  max deviation {err:.2e} dB')
    print(f'WORST {worst:.2e} dB ->', 'PASS' if worst < 1e-6 else 'FAIL')
    return 0 if worst < 1e-6 else 1

if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2]))
