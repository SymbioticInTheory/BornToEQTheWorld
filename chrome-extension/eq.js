// Shared by the popup and the audio page: the bands, the presets and the
// settings shape. The filters are Web Audio's own BiquadFilterNodes.

export const BANDS = [32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000];
export const BAND_LABELS = ['32', '64', '125', '250', '500', '1k', '2k', '4k', '8k', '16k'];
export const GAIN_RANGE = 12;          // each band: -12 to +12 dB
export const LEVEL_MIN = -40, LEVEL_MAX = 12;
export const PEAK_Q = 1.41;            // one octave wide, like a classic 10-band graphic EQ
export const Q_MIN = 0.3, Q_MAX = 8;

// Each band's frequency can move between its neighbours' usual frequencies
// (the ends go down to 20 Hz and up to 20 kHz).
export function freqRange(i) {
  const lo = i > 0 ? BANDS[i - 1] * 1.05 : 20;
  const hi = i < BANDS.length - 1 ? BANDS[i + 1] / 1.05 : 20000;
  return [lo, hi];
}
// A band's frequency and Q in these settings (older settings have neither).
export const bandFreq = (s, i) => (s.freqs && s.freqs[i]) || BANDS[i];
export const bandQ = (s, i) => (s.qs && s.qs[i]) || PEAK_Q;
export function bandTuned(s, i) { return bandFreq(s, i) !== BANDS[i] || bandQ(s, i) !== PEAK_Q; }

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
    const g = s.gains[i] || 0, fr = bandFreq(s, i), q = bandQ(s, i);
    if (smooth) {
      f.gain.setTargetAtTime(g, t, 0.007);
      f.frequency.setTargetAtTime(fr, t, 0.007);
      if (f.type === 'peaking') f.Q.setTargetAtTime(q, t, 0.007);
    } else {
      f.gain.value = g;
      f.frequency.value = fr;
      if (f.type === 'peaking') f.Q.value = q;
    }
  });
  const headroom = s.headroom ? -maxBoostDbFor(s) : 0;
  const lin = s.muted ? 0 : Math.pow(10, (s.level + headroom) / 20);
  if (smooth) chain.gain.gain.setTargetAtTime(lin, t, 0.007); else chain.gain.gain.value = lin;
}

// The same highest boost, worked out from the settings alone (on a throwaway
// context). Takes settings, or just an array of gains.
let probe = null;
function setProbe(s) {
  if (Array.isArray(s)) s = { gains: s };
  if (!probe) {
    const ctx = new OfflineAudioContext(1, 1, 48000);
    probe = buildChain(ctx);
  }
  probe.filters.forEach((f, i) => {
    f.gain.value = s.gains[i] || 0;
    f.frequency.value = bandFreq(s, i);
    if (f.type === 'peaking') f.Q.value = bandQ(s, i);
  });
}
export function maxBoostDbFor(s) {
  setProbe(s);
  return maxBoostDb(probe);
}

// The curve for drawing: dB at each of `freqs`.
export function responseDb(s, freqs) {
  setProbe(s);
  if (!(freqs instanceof Float32Array)) freqs = Float32Array.from(freqs);
  const n = freqs.length, mag = new Float32Array(n), phase = new Float32Array(n), total = new Float32Array(n);
  for (const f of probe.filters) {
    f.getFrequencyResponse(freqs, mag, phase);
    for (let i = 0; i < n; i++) total[i] += 20 * Math.log10(mag[i]);
  }
  return total;
}

// Picks a preset ('p:Rock') or one of your profiles ('u:name') into settings.
export function applyPreset(s, key, profiles) {
  // Presets are made for the usual frequencies and Q, so they bring those back.
  if (key.startsWith('p:') && PRESETS[key.slice(2)]) {
    s.gains = PRESETS[key.slice(2)].slice();
    delete s.freqs;
    delete s.qs;
  }
  if (key.startsWith('u:') && profiles[key.slice(2)]) {
    const p = profiles[key.slice(2)];
    s.gains = p.gains.slice();
    s.level = p.level;
    s.headroom = p.headroom;
    if (p.freqs) s.freqs = p.freqs.slice(); else delete s.freqs;
    if (p.qs) s.qs = p.qs.slice(); else delete s.qs;
  }
  s.preset = key;
  return s;
}
