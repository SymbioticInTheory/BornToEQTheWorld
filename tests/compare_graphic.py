"""Checks the GraphicEQ FIR against (1) an independent numpy version of the
same minimum-phase design and (2) the requested curve itself."""
import os, subprocess, sys, tempfile
import numpy as np

def parse(path):
    body = open(path).read().split(':', 1)[1]
    pts = [p.split() for p in body.split(';') if p.strip()]
    return np.array([float(p[0]) for p in pts]), np.array([float(p[1]) for p in pts])

def gain_at(fr, g, f):
    return np.interp(np.log(np.maximum(f, 1e-9)), np.log(fr), g)  # flat outside, linear in log f

def reference(fr, g, fs, L=16384):
    n = 2 * L
    f = np.arange(L) * fs / n
    mag = 10 ** (gain_at(fr, g, f) / 20)
    spec = np.empty(n); spec[:L] = mag; spec[n - 1 - np.arange(L)] = mag
    c = np.fft.ifft(np.log(np.maximum(spec, 1e-5)))       # real cepstrum
    fold = np.zeros(n, complex)
    fold[0] = c[0]; fold[L] = np.conj(c[L])
    fold[1:L] = c[1:L].real + c[n - 1:L:-1].real + 1j * (c[1:L].imag - c[n - 1:L:-1].imag)
    s = np.fft.fft(fold)
    h = np.fft.ifft(np.exp(s.real) * np.exp(1j * s.imag)).real[:L]
    return h * 0.5 * (1 + np.cos(2 * np.pi * np.arange(L) / n))

def main(binary, path):
    fr, g = parse(path)
    worst_ref = worst_curve = 0
    for fs in (44100, 48000, 96000):
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, 'fir.raw')
            subprocess.run([binary, path, str(fs), '1', out], check=True)
            taps = np.fromfile(out, dtype=np.float32).astype(float)
        ref = reference(fr, g, fs)
        e_ref = np.max(np.abs(taps - ref)) / np.max(np.abs(ref))
        test_f = np.geomspace(25, 18000, 400)
        H = np.fft.rfft(taps, 1 << 18)
        freq = np.fft.rfftfreq(1 << 18, 1 / fs)
        got = 20 * np.log10(np.abs(np.interp(test_f, freq, np.abs(H))))
        e_curve = np.max(np.abs(got - gain_at(fr, g, test_f)))
        worst_ref, worst_curve = max(worst_ref, e_ref), max(worst_curve, e_curve)
        print(f'fs={fs}: taps vs numpy design {e_ref:.1e} (relative), response vs curve {e_curve:.3f} dB')
    ok = worst_ref < 1e-4 and worst_curve < 0.25  # window smoothing; same in Equalizer APO
    print('PASS' if ok else 'FAIL')
    return 0 if ok else 1

if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2]))
