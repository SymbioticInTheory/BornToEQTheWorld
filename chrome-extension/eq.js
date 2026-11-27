// Shared by the popup and the audio page: the bands, the presets and the
// settings shape. The filters are Web Audio's own BiquadFilterNodes.

export const BANDS = [32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000];
export const BAND_LABELS = ['32', '64', '125', '250', '500', '1k', '2k', '4k', '8k', '16k'];
export const GAIN_RANGE = 12;          // each band: -12 to +12 dB
export const LEVEL_MIN = -40, LEVEL_MAX = 12;
const PEAK_Q = 1.41;                   // one octave wide, like a classic 10-band graphic EQ

// The first and last bands are shelves; the rest are peaks.
export function bandType(i) {
  return i === 0 ? 'lowshelf' : i === BANDS.length - 1 ? 'highshelf' : 'peaking';
}

export const PRESETS = {
  'Flat':         [0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
  'Bass boost':   [6, 5, 4, 2, 0, 0, 0, 0, 0, 0],
  'Bass cut':     [-6, -5, -3, -1, 0, 0, 0, 0, 0, 0],
  'Treble boost': [0, 0, 0, 0, 0, 0, 1, 3, 5, 6],
  'Treble cut':   [0, 0, 0, 0, 0, 0, -1, -3, -5, -6],
  'Vocal':        [-2, -2, -1, 1, 3, 4, 4, 2, 0, -1],
  'Speech':       [-6, -4, -1, 1, 3, 4, 3, 1, -1, -4],
  'Rock':         [5, 4, 2, -1, -2, -1, 1, 3, 4, 5],
  'Pop':          [-1, 1, 3, 4, 3, 0, -1, -1, 1, 2],
  'Jazz':         [3, 2, 1, 2, -1, -1, 0, 1, 2, 3],
  'Classical':    [4, 3, 2, 1, -1, -1, 0, 2, 3, 4],
  'Electronic':   [5, 4, 1, 0, -2, 1, 0, 1, 4, 5],
  'Hip-hop':      [5, 4, 2, 3, -1, -1, 1, -1, 2, 3],
  'Loudness':     [6, 4, 1, 0, 0, 0, 0, 1, 3, 4],
};

export function defaultSettings() {
  return { gains: PRESETS['Flat'].slice(), level: 0, headroom: true, preset: 'p:Flat' };
}

// Builds the filter chain on any AudioContext. Returns the nodes so they can be updated.
export function buildChain(ctx) {
  const filters = BANDS.map((f, i) => {
    const b = ctx.createBiquadFilter();
    b.type = bandType(i);
    b.frequency.value = f;
    if (b.type === 'peaking') b.Q.value = PEAK_Q;
    return b;
  });
  for (let i = 0; i + 1 < filters.length; i++) filters[i].connect(filters[i + 1]);
  const gain = ctx.createGain();
  filters[filters.length - 1].connect(gain);
  return { input: filters[0], output: gain, filters, gain };
}

// The EQ's highest boost in dB, across the audible range.
export function maxBoostDb(chain) {
  const n = 256, freqs = new Float32Array(n), mag = new Float32Array(n), phase = new Float32Array(n), total = new Float32Array(n);
  for (let i = 0; i < n; i++) freqs[i] = 20 * Math.pow(1000, i / (n - 1));
  for (const f of chain.filters) {
    f.getFrequencyResponse(freqs, mag, phase);
    for (let i = 0; i < n; i++) total[i] += 20 * Math.log10(mag[i]);
  }
  return Math.max(0, ...total);
}

// Applies settings to a chain. `smooth` glides the change over ~20 ms so it doesn't click.
export function applySettings(chain, s, ctx, smooth = true) {
  const t = ctx.currentTime;
  chain.filters.forEach((f, i) => {
    const g = s.gains[i] || 0;
    if (smooth) f.gain.setTargetAtTime(g, t, 0.007); else f.gain.value = g;
  });
  const headroom = s.headroom ? -maxBoostDbFor(s.gains) : 0;
  const lin = Math.pow(10, (s.level + headroom) / 20);
  if (smooth) chain.gain.gain.setTargetAtTime(lin, t, 0.007); else chain.gain.gain.value = lin;
}

// The same highest boost, worked out from the gains alone (on a throwaway context).
let probe = null;
export function maxBoostDbFor(gains) {
  if (!probe) {
    const ctx = new OfflineAudioContext(1, 1, 48000);
    probe = buildChain(ctx);
  }
  probe.filters.forEach((f, i) => { f.gain.value = gains[i] || 0; });
  return maxBoostDb(probe);
}

// The curve for drawing: dB at each of `freqs`.
export function responseDb(gains, freqs) {
  maxBoostDbFor(gains); // makes sure the probe is set to these gains
  if (!(freqs instanceof Float32Array)) freqs = Float32Array.from(freqs);
  const n = freqs.length, mag = new Float32Array(n), phase = new Float32Array(n), total = new Float32Array(n);
  for (const f of probe.filters) {
    f.getFrequencyResponse(freqs, mag, phase);
    for (let i = 0; i < n; i++) total[i] += 20 * Math.log10(mag[i]);
  }
  return total;
}
