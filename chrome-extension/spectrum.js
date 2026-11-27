// The spectrum window: everything BEQ plays, after the master. The audio page sends
// raw dB per FFT bin; this page averages (in power), smooths, tilts and draws it.
import { applyTheme, loadTheme } from './theme.js';

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
function showSettings() {
  for (const k of ['where', 'view', 'avgMs', 'block', 'overlap', 'slope', 'smooth']) $(k).value = String(cfg[k]);
  for (const k of ['lo', 'hi', 'fLo', 'fHi']) $(k).value = cfg[k];
  $('settings').hidden = !cfg.open;
  showHint();
  $('toggleSettings').classList.toggle('on', cfg.open);
  resize();
}

function bindSettings() {
  for (const k of ['where', 'view', 'avgMs', 'block', 'overlap', 'slope', 'smooth']) {
    $(k).addEventListener('change', () => {
      cfg[k] = k === 'view' || k === 'where' ? $(k).value : Number($(k).value);
      if (k === 'block' || k === 'where') reset();
      showHint();
      save();
    });
  }
  const limits = { lo: [-160, 20], hi: [-150, 30], fLo: [10, 10000], fHi: [200, 24000] };
  for (const k of Object.keys(limits)) {
    $(k).addEventListener('change', () => {
      const [a, b] = limits[k];
      let v = Math.min(b, Math.max(a, Number($(k).value) || DEFAULTS[k]));
      if (k === 'lo') v = Math.min(v, cfg.hi - 6);
      if (k === 'hi') v = Math.max(v, cfg.lo + 6);
      if (k === 'fLo') v = Math.min(v, cfg.fHi / 2);
      if (k === 'fHi') v = Math.max(v, cfg.fLo * 2);
      cfg[k] = v;
      $(k).value = v;
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
  if (!avg || idle) { readout(null); return; }

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
  // Where the mouse is: frequency and level.
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
  canvas.addEventListener('dblclick', reset);
  requestAnimationFrame(frame);
}

init();
