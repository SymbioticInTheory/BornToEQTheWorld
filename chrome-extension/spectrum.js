// The spectrum window: everything BEQ plays (before or after the EQ), with a tab's EQ
// drawn over it to tune. The audio page sends
// raw dB per FFT bin; this page averages (in power), smooths, tilts and draws it.
import { applyTheme, loadTheme } from './theme.js';
import { BANDS, GAIN_RANGE, PEAK_Q, Q_MIN, Q_MAX, bandType, bandFreq, bandQ, freqRange, responseDb } from './eq.js';

const $ = (id) => document.getElementById(id);
// Mastering defaults: a long average, 4.5 dB/oct tilt, 2/3 octave smoothing.
const DEFAULTS = { where: 'pre', view: 'both', avgMs: 6000, block: 8192, overlap: 0.88, slope: 4.5, smooth: 0.6667,
                   lo: -69, hi: -12, fLo: 20, fHi: 20000, open: true };
// A full-scale sine reads 0 dB (the analyser's Blackman window and scaling take 13.6 dB off).
const CAL_DB = -20 * Math.log10(0.42 / 2);

let cfg = { ...DEFAULTS };
let theme = { accent: 0, image: null };
let rate = 48000;
let avg = null, live = null, frames = 0;     // power per bin
let lastFrame = 0, lastPoll = 0, polling = false, idle = false;
let mouse = null;

const save = () => chrome.storage.local.set({ spectrum: cfg });

// ---------------------------------------------------------------- settings
// Smoothing steps in octaves, the same as SPAN's (and BEQ's plugin); 0 = off.
const SMOOTH_STEPS = [[0, 'Off'], [1 / 48, '1/48'], [1 / 24, '1/24'], [1 / 12, '1/12'], [1 / 9, '1/9'], [1 / 6, '1/6'],
  [1 / 5, '1/5'], [1 / 4, '1/4'], [1 / 3, '1/3'], [1 / 2, '1/2'], [2 / 3, '2/3'], [3 / 4, '3/4'], [1, '1'], [2, '2'], [2.5, '2.5']];
const smoothIndex = (o) => SMOOTH_STEPS.reduce((best, s, i) => (Math.abs(s[0] - o) < Math.abs(SMOOTH_STEPS[best][0] - o) ? i : best), 0);
// The dB range bar: the whole span the range can move in.
const RANGE_BOTTOM = -150, RANGE_TOP = 20;
// Knobs: name, range, how a value reads, and whether the knob turns on a log scale.
const KNOBS = [
  { key: 'avgMs', name: 'Average', min: 500, max: 31000, log: true, def: DEFAULTS.avgMs,
    get: (c) => (c.avgMs > 0 ? c.avgMs : 31000), set: (c, v) => { c.avgMs = v >= 30900 ? 0 : Math.round(v / 100) * 100; },
    text: (c) => (c.avgMs > 0 ? (c.avgMs / 1000).toFixed(1) + ' s' : 'Until reset'),
    tip: 'How long the average looks back. All the way up: until you reset it' },
  { key: 'overlap', name: 'Overlap', min: 0, max: 0.95, def: DEFAULTS.overlap,
    set: (c, v) => { c.overlap = Math.round(v * 100) / 100; }, text: (c) => Math.round(c.overlap * 100) + '%',
    tip: 'How much each block overlaps the last: higher updates more often' },
  { key: 'slope', name: 'Slope', min: -3, max: 9, def: DEFAULTS.slope,
    set: (c, v) => { c.slope = Math.round(v * 2) / 2; }, text: (c) => c.slope.toFixed(1) + ' dB/oct',
    tip: 'Tilts the display around 1 kHz, so a balanced mix looks flat (4.5 is common for mastering)' },
  { key: 'smooth', name: 'Smoothing', min: 0, max: SMOOTH_STEPS.length - 1, def: smoothIndex(DEFAULTS.smooth),
    get: (c) => smoothIndex(c.smooth), set: (c, v) => { c.smooth = SMOOTH_STEPS[Math.round(v)][0]; }, text: (c) => smoothText(c.smooth),
    tip: 'Averages over this many octaves, so the shape is easier to read (also in the list on the left)' },
  { key: 'lo', name: 'Range low', min: -150, max: 10, def: DEFAULTS.lo,
    set: (c, v) => { c.lo = Math.min(Math.round(v), c.hi - 6); }, text: (c) => c.lo + ' dB', tip: 'Level at the bottom of the graph' },
  { key: 'hi', name: 'Range high', min: -140, max: 20, def: DEFAULTS.hi,
    set: (c, v) => { c.hi = Math.max(Math.round(v), c.lo + 6); }, text: (c) => c.hi + ' dB', tip: 'Level at the top of the graph' },
  { key: 'fLo', name: 'Freq low', min: 10, max: 1000, log: true, def: DEFAULTS.fLo,
    set: (c, v) => { c.fLo = Math.min(Math.round(v), c.fHi / 2); }, text: (c) => hzText(c.fLo), tip: 'Lowest frequency shown' },
  { key: 'fHi', name: 'Freq high', min: 1000, max: 24000, log: true, def: DEFAULTS.fHi,
    set: (c, v) => { c.fHi = Math.max(Math.round(v / 100) * 100, c.fLo * 2); }, text: (c) => hzText(c.fHi), tip: 'Highest frequency shown' },
];
const hzText = (f) => (f >= 1000 ? (f / 1000).toFixed(f >= 10000 ? 1 : 2) + ' kHz' : Math.round(f) + ' Hz');
function smoothText(o) {
  const i = smoothIndex(o);
  return i === 0 ? 'Off' : SMOOTH_STEPS[i][1] + ' oct';
}

// A knob like the mixer's master: drag up or down (Shift: fine), scroll, double-click for the default.
const toPos = (k, v) => (k.log ? Math.log(v / k.min) / Math.log(k.max / k.min) : (v - k.min) / (k.max - k.min));
const fromPos = (k, p) => { p = Math.min(1, Math.max(0, p)); return k.log ? k.min * Math.pow(k.max / k.min, p) : k.min + p * (k.max - k.min); };
const knobPoint = (deg, r) => [24 + r * Math.sin((deg * Math.PI) / 180), 24 - r * Math.cos((deg * Math.PI) / 180)];
const arc = (a0, a1, r) => {
  const [x0, y0] = knobPoint(a0, r), [x1, y1] = knobPoint(a1, r);
  return `M ${x0} ${y0} A ${r} ${r} 0 ${a1 - a0 > 180 ? 1 : 0} 1 ${x1} ${y1}`;
};
function buildKnobs() {
  const box = $('knobs');
  for (const k of KNOBS) {
    const el = document.createElement('div');
    el.className = 'knob';
    el.title = k.tip + '. Drag up or down (Shift: fine), scroll, or double-click for the default.';
    el.innerHTML = `<span class="label">${k.name}</span>
      <svg viewBox="0 0 48 48" width="44" height="44" tabindex="0">
        <circle cx="24" cy="24" r="15" class="knob-body"/><path class="knob-track"/><path class="knob-arc"/>
        <line x1="24" y1="24" x2="24" y2="12" class="knob-pointer"/></svg>
      <span class="value"></span>`;
    const svg = el.querySelector('svg');
    const value = () => (k.get ? k.get(cfg) : cfg[k.key]);
    const put = (p) => { k.set(cfg, fromPos(k, p)); onKnob(k.key); };
    let startY = 0, startP = 0;
    const move = (e) => put(startP + (startY - e.clientY) / (e.shiftKey ? 800 : 160));
    svg.addEventListener('pointerdown', (e) => { startY = e.clientY; startP = toPos(k, value()); svg.setPointerCapture(e.pointerId); svg.addEventListener('pointermove', move); });
    const end = () => svg.removeEventListener('pointermove', move);
    svg.addEventListener('pointerup', end);
    svg.addEventListener('pointercancel', end);
    svg.addEventListener('dblclick', () => { k.set(cfg, k.def); onKnob(k.key); });
    svg.addEventListener('wheel', (e) => { e.preventDefault(); put(toPos(k, value()) + (e.deltaY < 0 ? 0.01 : -0.01) * (e.shiftKey ? 0.25 : 1)); }, { passive: false });
    k.el = el;
    box.appendChild(el);
  }
}
function drawKnobs() {
  for (const k of KNOBS) {
    const p = toPos(k, k.get ? k.get(cfg) : cfg[k.key]), a = -135 + 270 * Math.min(1, Math.max(0, p));
    const zero = k.min < 0 && k.max > 0 ? -135 + 270 * toPos(k, 0) : -135;
    k.el.querySelector('.knob-track').setAttribute('d', arc(-135, 135, 21));
    k.el.querySelector('.knob-arc').setAttribute('d', Math.abs(a - zero) < 0.5 ? '' : arc(Math.min(a, zero), Math.max(a, zero), 21));
    const [x, y] = knobPoint(a, 11);
    const ptr = k.el.querySelector('.knob-pointer');
    ptr.setAttribute('x2', x); ptr.setAttribute('y2', y);
    k.el.querySelector('.value').textContent = k.text(cfg);
  }
  $('smooth').value = String(smoothIndex(cfg.smooth));
  drawRange();
}
let saveTimer = null;
function onKnob() {
  drawKnobs();
  clearTimeout(saveTimer);
  saveTimer = setTimeout(save, 400); // written once you stop turning
  draw();
}

function showSettings() {
  for (const k of ['where', 'view', 'block']) $(k).value = String(cfg[k]);
  $('settings').hidden = !cfg.open;
  $('toggleSettings').textContent = cfg.open ? 'Settings ▾' : 'Settings ▸';
  $('toggleSettings').classList.toggle('on', cfg.open);
  drawKnobs();
  showHint();
  resize();
}

// ---------------------------------------------------------------- dB range bar (like SPAN's)
// Drag its ends to zoom in or out, the middle to move it, scroll on it to zoom,
// double-click for the default.
function drawRange() {
  const t = $('rangeThumb'), span = RANGE_TOP - RANGE_BOTTOM;
  t.style.top = ((RANGE_TOP - cfg.hi) / span) * 100 + '%';
  t.style.height = ((cfg.hi - cfg.lo) / span) * 100 + '%';
}
function setRange(lo, hi) {
  const span = Math.min(RANGE_TOP - RANGE_BOTTOM, Math.max(6, hi - lo));
  hi = Math.min(RANGE_TOP, Math.max(RANGE_BOTTOM + span, hi));
  cfg.hi = Math.round(hi * 2) / 2;
  cfg.lo = Math.round((hi - span) * 2) / 2;
  onKnob();
}
function bindRange() {
  const bar = $('range');
  const dbAt = (clientY) => { const r = bar.getBoundingClientRect(); return RANGE_TOP - ((clientY - r.top) / r.height) * (RANGE_TOP - RANGE_BOTTOM); };
  const yOf = (db) => { const r = bar.getBoundingClientRect(); return r.top + ((RANGE_TOP - db) / (RANGE_TOP - RANGE_BOTTOM)) * r.height; };
  let drag = null;
  const part = (clientY) => {
    const dHi = Math.abs(clientY - yOf(cfg.hi)), dLo = Math.abs(clientY - yOf(cfg.lo));
    return dHi < 7 && dHi <= dLo ? 'hi' : dLo < 7 ? 'lo' : 'move';
  };
  bar.addEventListener('pointerdown', (e) => {
    const p = part(e.clientY);
    if (p === 'move' && (e.clientY < yOf(cfg.hi) || e.clientY > yOf(cfg.lo))) { // outside the thumb: move it there
      const mid = dbAt(e.clientY), half = (cfg.hi - cfg.lo) / 2;
      setRange(mid - half, mid + half);
    }
    drag = { p, y: e.clientY, lo: cfg.lo, hi: cfg.hi };
    bar.setPointerCapture(e.pointerId);
    bar.classList.add('active');
  });
  bar.addEventListener('pointermove', (e) => {
    if (!drag) { bar.style.cursor = part(e.clientY) === 'move' ? 'grab' : 'ns-resize'; return; }
    const r = bar.getBoundingClientRect();
    const d = -((e.clientY - drag.y) / r.height) * (RANGE_TOP - RANGE_BOTTOM) * (e.shiftKey ? 0.25 : 1);
    if (drag.p === 'hi') setRange(drag.lo, Math.max(drag.lo + 6, drag.hi + d));
    else if (drag.p === 'lo') setRange(Math.min(drag.hi - 6, drag.lo + d), drag.hi);
    else setRange(drag.lo + d, drag.hi + d);
  });
  const end = () => { drag = null; bar.classList.remove('active'); };
  bar.addEventListener('pointerup', end);
  bar.addEventListener('pointercancel', end);
  bar.addEventListener('wheel', (e) => {
    e.preventDefault();
    const mid = (cfg.lo + cfg.hi) / 2, half = ((cfg.hi - cfg.lo) / 2) * (e.deltaY < 0 ? 1 / 1.15 : 1.15);
    setRange(mid - half, mid + half);
  }, { passive: false });
  bar.addEventListener('dblclick', () => setRange(DEFAULTS.lo, DEFAULTS.hi));
}

function bindSettings() {
  const sm = $('smooth');
  SMOOTH_STEPS.forEach(([, name], i) => sm.add(new Option(i === 0 ? 'Off' : name + ' oct', String(i))));
  sm.addEventListener('change', () => { cfg.smooth = SMOOTH_STEPS[Number(sm.value)][0]; onKnob(); });
  bindRange();
  for (const k of ['where', 'view', 'block']) {
    $(k).addEventListener('change', () => {
      cfg[k] = k === 'block' ? Number($(k).value) : $(k).value;
      if (k === 'block' || k === 'where') reset();
      showHint();
      save();
    });
  }
  $('defaults').addEventListener('click', () => { cfg = { ...DEFAULTS, open: cfg.open }; reset(); save(); showSettings(); });
  $('toggleSettings').addEventListener('click', () => { cfg.open = !cfg.open; save(); showSettings(); });
  $('reset').addEventListener('click', reset);
}

function showHint() {
  $('hint').textContent = cfg.where === 'pre'
    ? 'Everything playing through BEQ, before any EQ' : 'Everything playing through BEQ, after the EQ and master';
}

function reset() { avg = null; live = null; frames = 0; }

// ---------------------------------------------------------------- data
async function poll(now) {
  polling = true;
  let r = null;
  try { r = await chrome.runtime.sendMessage({ target: 'offscreen', type: 'masterSpectrum', fftSize: cfg.block, where: cfg.where }); } catch (_) {}
  polling = false;
  idle = !r || !r.playing;
  $('idle').hidden = !idle;
  if (!r) return;
  rate = r.rate;
  const n = r.db.length;
  if (!avg || avg.length !== n) { avg = new Float64Array(n); live = new Float64Array(n); frames = 0; }
  const dt = Math.max(0.001, (now - (lastPoll || now)) / 1000) || 0.03;
  lastPoll = now;
  frames++;
  // Average: exponential over the average time, or the plain mean until reset.
  const a = cfg.avgMs > 0 ? 1 - Math.exp(-dt / (cfg.avgMs / 1000)) : 1 / frames;
  const fall = Math.exp(-dt / 0.25); // live: jumps up, falls back over about a quarter second
  for (let i = 0; i < n; i++) {
    const p = Math.pow(10, r.db[i] / 10);
    avg[i] = frames === 1 ? p : avg[i] + a * (p - avg[i]);
    live[i] = Math.max(p, live[i] * fall);
  }
}

// Level per display point. Each point first takes its own slice of bins (the loudest
// one when smoothing is off, so peaks keep their height; else the mean power), then
// with smoothing the points are blended with a Gaussian about `smooth` octaves wide.
function curve(power, points) {
  const n = power.length, hz = rate / (2 * n);
  const sum = new Float64Array(n + 1);
  for (let i = 0; i < n; i++) sum[i + 1] = sum[i] + power[i];
  const span = Math.log2(cfg.fHi / cfg.fLo), step = span / (points - 1);
  const pts = new Float64Array(points), freq = new Float64Array(points);
  for (let j = 0; j < points; j++) {
    const f = cfg.fLo * Math.pow(2, step * j);
    freq[j] = f;
    const b0 = f * Math.pow(2, -step / 2) / hz, b1 = f * Math.pow(2, step / 2) / hz;
    const i0 = Math.max(0, Math.floor(b0)), i1 = Math.min(n, Math.ceil(b1));
    if (b1 - b0 < 1 || i1 <= i0) { // narrower than a bin: between the two nearest bins
      const x = Math.min(n - 1.001, Math.max(0, f / hz)), i = Math.floor(x), t = x - i;
      pts[j] = power[i] * (1 - t) + power[i + 1] * t;
    } else if (cfg.smooth > 0) {
      pts[j] = (sum[i1] - sum[i0]) / (i1 - i0);
    } else {
      let p = 0;
      for (let i = i0; i < i1; i++) p = Math.max(p, power[i]);
      pts[j] = p;
    }
  }
  let sm = pts;
  if (cfg.smooth > 0) {
    // Gaussian over log frequency; its sigma makes the -6 dB width about `smooth` octaves.
    const sigma = cfg.smooth / 2.355 / step, reach = Math.ceil(sigma * 3);
    const k = new Float64Array(2 * reach + 1);
    for (let i = -reach; i <= reach; i++) k[i + reach] = Math.exp(-(i * i) / (2 * sigma * sigma));
    sm = new Float64Array(points);
    for (let j = 0; j < points; j++) {
      let a = 0, w = 0;
      for (let i = -reach; i <= reach; i++) {
        const q = j + i;
        if (q < 0 || q >= points) continue;
        a += pts[q] * k[i + reach]; w += k[i + reach];
      }
      sm[j] = a / w;
    }
  }
  const out = new Float64Array(points);
  for (let j = 0; j < points; j++)
    out[j] = 10 * Math.log10(Math.max(1e-30, sm[j])) + CAL_DB + cfg.slope * Math.log2(freq[j] / 1000);
  return out;
}

// ---------------------------------------------------------------- drawing
const canvas = $('plot'), g = canvas.getContext('2d');
function resize() {
  const r = canvas.getBoundingClientRect(), d = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, Math.round(r.width * d));
  canvas.height = Math.max(1, Math.round(r.height * d));
}

function draw() {
  const W = canvas.width, H = canvas.height, d = window.devicePixelRatio || 1;
  const span = Math.log2(cfg.fHi / cfg.fLo);
  const x = (f) => (Math.log2(f / cfg.fLo) / span) * W;
  const y = (db) => ((cfg.hi - db) / (cfg.hi - cfg.lo)) * H;
  g.clearRect(0, 0, W, H);
  g.lineWidth = 1;
  g.font = `600 ${11 * d}px Barlow, sans-serif`;
  g.fillStyle = 'rgba(166,164,159,0.85)';
  for (const f of [20, 30, 50, 100, 200, 300, 500, 1000, 2000, 3000, 5000, 10000, 20000]) {
    if (f <= cfg.fLo || f >= cfg.fHi) continue;
    const major = /^[125]/.test(String(f));
    g.strokeStyle = major ? 'rgba(255,255,255,0.09)' : 'rgba(255,255,255,0.04)';
    g.beginPath(); g.moveTo(x(f), 0); g.lineTo(x(f), H); g.stroke();
    if (major) g.fillText(f >= 1000 ? f / 1000 + 'k' : String(f), x(f) + 3 * d, H - 5 * d);
  }
  const step = cfg.hi - cfg.lo > 60 ? 12 : 6;
  for (let db = Math.ceil(cfg.lo / step) * step; db <= cfg.hi; db += step) {
    g.strokeStyle = 'rgba(255,255,255,0.07)';
    g.beginPath(); g.moveTo(0, y(db)); g.lineTo(W, y(db)); g.stroke();
    if (db > cfg.lo + 2 && db < cfg.hi - 2) g.fillText(String(db), 4 * d, y(db) - 3 * d);
  }
  if (!avg || idle) { drawEq(); readout(eqReadout()); return; }

  const points = Math.max(64, Math.round(W / (2 * d)));
  const color = getComputedStyle(document.documentElement).getPropertyValue('--accent').trim();
  const line = (vals) => {
    g.beginPath();
    for (let j = 0; j < points; j++) {
      const px = (j / (points - 1)) * W, py = Math.min(H + 2, Math.max(-2, y(vals[j])));
      j ? g.lineTo(px, py) : g.moveTo(px, py);
    }
  };
  let avgDb = null, liveDb = null;
  if (cfg.view !== 'avg') {
    liveDb = curve(live, points);
    line(liveDb);
    g.strokeStyle = cfg.view === 'live' ? color : 'rgba(210,208,202,0.45)';
    g.lineWidth = 1.25 * d;
    g.stroke();
  }
  if (cfg.view !== 'live') {
    avgDb = curve(avg, points);
    line(avgDb);
    g.lineTo(W, H + 2); g.lineTo(0, H + 2); g.closePath();
    const fill = g.createLinearGradient(0, 0, 0, H);
    fill.addColorStop(0, color + '55');
    fill.addColorStop(1, color + '08');
    g.fillStyle = fill;
    g.fill();
    line(avgDb);
    g.strokeStyle = color;
    g.lineWidth = 2 * d;
    g.lineJoin = 'round';
    g.stroke();
  }
  drawEq();
  // Where the mouse is: frequency and level.
  if (dragBand >= 0) { readout(eqReadout()); return; }
  if (mouse) {
    const j = Math.round((mouse.x * d / W) * (points - 1));
    if (j >= 0 && j < points) {
      const f = cfg.fLo * Math.pow(2, (span * j) / (points - 1));
      g.strokeStyle = 'rgba(255,255,255,0.25)';
      g.beginPath(); g.moveTo(mouse.x * d, 0); g.lineTo(mouse.x * d, H); g.stroke();
      const v = (avgDb || liveDb)[j];
      readout(`${f >= 1000 ? (f / 1000).toFixed(2) + ' kHz' : Math.round(f) + ' Hz'}  ${v.toFixed(1)} dB`);
      return;
    }
  }
  readout(null);
}
function readout(t) { $('readout').textContent = t || ''; }

// ---------------------------------------------------------------- a tab's EQ over the spectrum
// Drawn on its own +-12 dB scale (labels on the right). Drag a point for frequency and
// gain, scroll on it for Q, double-click it for 0 dB. Changes go to the tab straight away.
let eqTabs = {};            // tabs that are on: id -> settings (storage.session "tabs")
let tuneTab = null;         // the tab whose EQ is drawn, or null
let dragBand = -1, pushTimer = null;
const tuned = () => (tuneTab != null ? eqTabs[tuneTab] : null);
const eqX = (f) => (Math.log2(f / cfg.fLo) / Math.log2(cfg.fHi / cfg.fLo)) * canvas.width;
const eqY = (db) => canvas.height / 2 - (db / GAIN_RANGE) * (canvas.height / 2 - 10 * (window.devicePixelRatio || 1));
function nodePos(s, i) { return [eqX(bandFreq(s, i)), eqY(s.gains[i] || 0)]; }
function nodeAt(px, py) {
  const s = tuned();
  if (!s) return -1;
  const d = window.devicePixelRatio || 1;
  let best = -1, bestDist = 12 * d;
  for (let i = 0; i < BANDS.length; i++) {
    const [x, y] = nodePos(s, i), dist = Math.hypot(x - px, y - py);
    if (dist < bestDist) { bestDist = dist; best = i; }
  }
  return best;
}
function drawEq() {
  const s = tuned();
  if (!s) return;
  const W = canvas.width, H = canvas.height, d = window.devicePixelRatio || 1;
  g.font = `600 ${10 * d}px Barlow, sans-serif`;
  g.fillStyle = 'rgba(232,228,220,0.55)';
  g.textAlign = 'right';
  for (const db of [-12, -6, 0, 6, 12]) g.fillText((db > 0 ? '+' : '') + db, W - 4 * d, eqY(db) + (db === 12 ? 10 : db === -12 ? -3 : 4) * d);
  g.textAlign = 'left';
  g.strokeStyle = 'rgba(232,228,220,0.16)';
  g.lineWidth = 1;
  g.beginPath(); g.moveTo(0, eqY(0)); g.lineTo(W, eqY(0)); g.stroke();
  const n = Math.max(100, Math.round(W / (3 * d))), freqs = new Float32Array(n);
  for (let i = 0; i < n; i++) freqs[i] = cfg.fLo * Math.pow(cfg.fHi / cfg.fLo, i / (n - 1));
  const db = responseDb(s, freqs);
  g.beginPath();
  for (let i = 0; i < n; i++) (i ? g.lineTo : g.moveTo).call(g, (i / (n - 1)) * W, eqY(db[i]));
  g.strokeStyle = '#F2EEE6';
  g.lineWidth = 2.5 * d;
  g.lineJoin = 'round';
  g.stroke();
  g.font = `700 ${11 * d}px "Barlow Condensed", sans-serif`;
  g.textAlign = 'center';
  for (let i = 0; i < BANDS.length; i++) {
    const [x, y] = nodePos(s, i), r = 8 * d;
    if (x < -r || x > W + r) continue;
    g.beginPath(); g.arc(x, y, r, 0, Math.PI * 2);
    g.fillStyle = i === dragBand ? 'rgba(242,238,230,0.45)' : '#0B0C0D';
    g.fill();
    g.strokeStyle = '#F2EEE6'; g.lineWidth = 2 * d; g.stroke();
    g.fillStyle = '#F2EEE6';
    g.fillText(String(i + 1), x, y + 4 * d);
  }
  g.textAlign = 'left';
}
function eqReadout() {
  const s = tuned();
  if (!s || dragBand < 0) return null;
  const i = dragBand, q = bandType(i) === 'peaking' ? '  Q ' + bandQ(s, i).toFixed(2) : '  shelf';
  return `Band ${i + 1}: ${hzText(bandFreq(s, i))}  ${(s.gains[i] > 0 ? '+' : '') + (s.gains[i] || 0).toFixed(1)} dB${q}`;
}
function tune(i, edit) {
  const s = tuned();
  if (!s) return;
  if (!s.freqs) s.freqs = BANDS.slice();
  if (!s.qs) s.qs = BANDS.map(() => PEAK_Q);
  edit(s, i);
  s.preset = '';
  draw();
  if (pushTimer) return; // at most every 30 ms
  pushTimer = setTimeout(() => {
    pushTimer = null;
    if (tuned()) chrome.runtime.sendMessage({ target: 'background', type: 'set', tabId: tuneTab, settings: tuned() });
  }, 30);
}
async function refreshTabs() {
  const { tabs = {} } = await chrome.storage.session.get('tabs');
  if (dragBand < 0) eqTabs = tabs;
  const sel = $('tune'), ids = Object.keys(tabs).map(Number);
  if (tuneTab != null && !tabs[tuneTab]) tuneTab = null;
  if (tuneTab == null && ids.length && !sel.dataset.chosenNone) tuneTab = ids[0];
  const opts = [['', 'None']];
  for (const id of ids) {
    let title = 'Tab ' + id;
    try { title = (await chrome.tabs.get(id)).title || title; } catch (_) {}
    opts.push([String(id), title]);
  }
  sel.innerHTML = '';
  for (const [v, t] of opts) { const o = document.createElement('option'); o.value = v; o.textContent = t; sel.appendChild(o); }
  sel.value = tuneTab != null ? String(tuneTab) : '';
  draw();
}
function bindEq() {
  $('tune').addEventListener('change', (e) => {
    tuneTab = e.target.value ? Number(e.target.value) : null;
    e.target.dataset.chosenNone = tuneTab == null ? '1' : '';
    draw();
  });
  const pos = (e) => { const d = window.devicePixelRatio || 1; return [e.offsetX * d, e.offsetY * d]; };
  canvas.addEventListener('pointerdown', (e) => {
    dragBand = nodeAt(...pos(e));
    if (dragBand >= 0) canvas.setPointerCapture(e.pointerId);
  });
  canvas.addEventListener('pointermove', (e) => {
    const [px, py] = pos(e);
    canvas.style.cursor = dragBand >= 0 || nodeAt(px, py) >= 0 ? 'grab' : 'crosshair';
    if (dragBand < 0) return;
    tune(dragBand, (s, i) => {
      const [lo, hi] = freqRange(i);
      const f = cfg.fLo * Math.pow(cfg.fHi / cfg.fLo, Math.min(1, Math.max(0, px / canvas.width)));
      s.freqs[i] = Math.round(Math.min(hi, Math.max(lo, f)));
      const d = window.devicePixelRatio || 1;
      const db = ((canvas.height / 2 - py) / (canvas.height / 2 - 10 * d)) * GAIN_RANGE;
      s.gains[i] = Math.round(Math.min(GAIN_RANGE, Math.max(-GAIN_RANGE, db)) * 10) / 10;
    });
  });
  const end = () => { if (dragBand >= 0) { dragBand = -1; draw(); } };
  canvas.addEventListener('pointerup', end);
  canvas.addEventListener('pointercancel', end);
  canvas.addEventListener('wheel', (e) => {
    const i = nodeAt(...pos(e));
    if (i < 0 || bandType(i) !== 'peaking') return;
    e.preventDefault();
    tune(i, (s) => { s.qs[i] = Math.round(Math.min(Q_MAX, Math.max(Q_MIN, bandQ(s, i) * (e.deltaY < 0 ? 1.1 : 1 / 1.1))) * 100) / 100; });
  }, { passive: false });
  chrome.storage.onChanged.addListener((changes, area) => { if (area === 'session' && changes.tabs) refreshTabs(); });
}

// ---------------------------------------------------------------- loop
async function frame(now) {
  // One request at a time; the interval follows the block size and overlap.
  const interval = Math.max(16, (cfg.block / rate) * (1 - cfg.overlap) * 1000);
  if (!polling && now - lastFrame >= interval && !document.hidden) {
    lastFrame = now;
    await poll(now);
    draw();
  } else if (mouse) draw();
  requestAnimationFrame(frame);
}

async function init() {
  const s = await chrome.storage.local.get('spectrum');
  cfg = { ...DEFAULTS, ...(s.spectrum || {}) };
  theme = await loadTheme();
  await applyTheme(theme, $('title'), null);
  await document.fonts.load('600 11px Barlow').catch(() => {});
  buildKnobs();
  bindSettings();
  showSettings();
  chrome.storage.onChanged.addListener((changes, area) => {
    if (area === 'local' && (changes.accent || changes.bgImage || changes.imageColours))
      loadTheme().then((t) => { theme = t; applyTheme(theme, $('title'), null); });
  });
  let sizeTimer = null;
  window.addEventListener('resize', () => {
    resize(); draw();
    clearTimeout(sizeTimer);
    sizeTimer = setTimeout(() => chrome.storage.local.set({ spectrumSize: { width: window.outerWidth, height: window.outerHeight } }), 400);
  });
  canvas.addEventListener('mousemove', (e) => { mouse = { x: e.offsetX }; });
  canvas.addEventListener('mouseleave', () => { mouse = null; draw(); });
  canvas.addEventListener('dblclick', (e) => {
    const d = window.devicePixelRatio || 1, i = nodeAt(e.offsetX * d, e.offsetY * d);
    if (i >= 0) tune(i, (s) => { s.gains[i] = 0; }); else reset();
  });
  bindEq();
  await refreshTabs();
  requestAnimationFrame(frame);
}

init();
