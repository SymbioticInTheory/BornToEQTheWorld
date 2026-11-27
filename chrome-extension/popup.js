import { BANDS, BAND_LABELS, GAIN_RANGE, PRESETS, defaultSettings, responseDb, maxBoostDbFor, applyPreset } from './eq.js';

// popup.html?tab=ID&from=mixer edits that tab inside the mixer window.
const params = new URLSearchParams(location.search);
const fixedTab = params.has('tab') ? Number(params.get('tab')) : null;
const fromMixer = params.get('from') === 'mixer';

const ACCENTS = ['#C42B2B', '#E46A2E', '#D9D5CD', '#7D78D6'];
const ACCENT_NAMES = ['Blood red', 'Ember', 'Bone', 'Frost'];
const $ = (id) => document.getElementById(id);

let tabId = null;
let active = false;
let settings = defaultSettings();
let profiles = {};   // your saved profiles: name -> settings
let accent = 0;

// ---------------------------------------------------------------- storage
async function loadLocal() {
  const s = await chrome.storage.local.get(['profiles', 'accent', 'lastSettings']);
  profiles = s.profiles || {};
  accent = s.accent ?? 0;
  return s.lastSettings || null;
}
const saveProfiles = () => chrome.storage.local.set({ profiles });

// ---------------------------------------------------------------- talking to the background
const send = (msg) => chrome.runtime.sendMessage({ target: 'background', ...msg });

async function startTab() {
  try {
    const streamId = await chrome.tabCapture.getMediaStreamId({ targetTabId: tabId });
    settings.muted = false;
    const r = await send({ type: 'start', tabId, streamId, settings });
    if (!r || !r.ok) throw new Error(r && r.error);
    active = true;
    status('');
  } catch (e) {
    active = false;
    const msg = e && e.message ? String(e.message) : '';
    status(/invoked/.test(msg)
      ? 'Chrome needs one click on the BEQ icon while you are on this tab first.'
      : "This tab can't be EQ'd" + (msg ? ': ' + msg : '.'));
  }
  render();
}

async function stopTab() {
  await send({ type: 'stop', tabId });
  active = false;
  render();
}

let pushTimer = null;
function changed() {
  render();
  chrome.storage.local.set({ lastSettings: settings });
  if (!active) return;
  // Sliders can fire very often; pass changes on at most every 30 ms.
  if (pushTimer) return;
  pushTimer = setTimeout(() => {
    pushTimer = null;
    send({ type: 'set', tabId, settings });
  }, 30);
}

function status(text) { $('status').textContent = text; }

// ---------------------------------------------------------------- controls
function buildBands() {
  const box = $('bands');
  BANDS.forEach((f, i) => {
    const col = document.createElement('div');
    col.className = 'band';
    col.innerHTML = `<span class="value"></span>
      <input type="range" min="${-GAIN_RANGE}" max="${GAIN_RANGE}" step="0.5" value="0"
             title="${BAND_LABELS[i]} Hz. Double-click for 0 dB">
      <span class="label">${BAND_LABELS[i]}</span>`;
    const input = col.querySelector('input');
    input.addEventListener('input', () => {
      settings.gains[i] = Number(input.value);
      settings.preset = '';
      changed();
    });
    input.addEventListener('dblclick', () => {
      settings.gains[i] = 0;
      settings.preset = '';
      changed();
    });
    box.appendChild(col);
  });
}

function buildPresetList() {
  const sel = $('preset');
  sel.innerHTML = '';
  const add = (parent, value, label) => {
    const o = document.createElement('option');
    o.value = value;
    o.textContent = label;
    parent.appendChild(o);
  };
  add(sel, '', 'Custom (not saved)');
  const std = document.createElement('optgroup');
  std.label = 'Presets';
  Object.keys(PRESETS).forEach((n) => add(std, 'p:' + n, n));
  sel.appendChild(std);
  const names = Object.keys(profiles).sort((a, b) => a.localeCompare(b));
  if (names.length) {
    const mine = document.createElement('optgroup');
    mine.label = 'Your profiles';
    names.forEach((n) => add(mine, 'u:' + n, n));
    sel.appendChild(mine);
  }
}

function buildSwatches() {
  const box = $('swatches');
  ACCENTS.forEach((c, i) => {
    const b = document.createElement('button');
    b.className = 'swatch';
    b.title = ACCENT_NAMES[i];
    b.innerHTML = `<span style="background:${c}"></span>`;
    b.addEventListener('click', () => {
      accent = i;
      chrome.storage.local.set({ accent });
      render();
    });
    box.appendChild(b);
  });
}

// ---------------------------------------------------------------- drawing
function fmt(db) { return (db > 0 ? '+' : '') + db.toFixed(1); }

function drawCurve() {
  const canvas = $('curve'), g = canvas.getContext('2d');
  const W = canvas.width, H = canvas.height, range = GAIN_RANGE + 3;
  const x = (f) => (Math.log(f / 20) / Math.log(1000)) * W;
  const y = (db) => H / 2 - (db / range) * (H / 2 - 8);
  g.clearRect(0, 0, W, H);
  // Grid, like the plugin's.
  g.lineWidth = 1;
  g.strokeStyle = 'rgba(255,255,255,0.08)';
  g.font = '600 18px Barlow, sans-serif';
  g.fillStyle = 'rgba(166,164,159,0.8)';
  for (const f of [50, 100, 200, 500, 1000, 2000, 5000, 10000]) {
    g.beginPath(); g.moveTo(x(f), 0); g.lineTo(x(f), H); g.stroke();
  }
  for (const db of [-12, -6, 6, 12]) {
    g.beginPath(); g.moveTo(0, y(db)); g.lineTo(W, y(db)); g.stroke();
  }
  g.strokeStyle = 'rgba(255,255,255,0.18)';
  g.beginPath(); g.moveTo(0, y(0)); g.lineTo(W, y(0)); g.stroke();
  [['100', 100], ['1k', 1000], ['10k', 10000]].forEach(([t, f]) => g.fillText(t, x(f) + 4, H - 6));

  const n = 300, freqs = new Float32Array(n);
  for (let i = 0; i < n; i++) freqs[i] = 20 * Math.pow(1000, i / (n - 1));
  const db = responseDb(settings.gains, freqs);
  const color = getComputedStyle(document.documentElement).getPropertyValue('--accent').trim();
  // Soft fill under the curve, then the line.
  g.beginPath();
  g.moveTo(0, y(0));
  for (let i = 0; i < n; i++) g.lineTo(x(freqs[i]), y(db[i]));
  g.lineTo(W, y(0));
  g.closePath();
  g.fillStyle = color + '22';
  g.fill();
  g.beginPath();
  for (let i = 0; i < n; i++) (i ? g.lineTo : g.moveTo).call(g, x(freqs[i]), y(db[i]));
  g.strokeStyle = active ? color : 'rgba(110,113,120,1)';
  g.lineWidth = 5;
  g.lineJoin = 'round';
  g.stroke();
}

function render() {
  document.documentElement.style.setProperty('--accent', ACCENTS[accent]);
  $('title').src = `assets/title${accent}.png`;
  [...$('swatches').children].forEach((b, i) => b.classList.toggle('on', i === accent));

  $('power').textContent = active ? 'On' : 'Off';
  $('power').classList.toggle('on', active);

  [...$('bands').children].forEach((col, i) => {
    const input = col.querySelector('input');
    if (Number(input.value) !== settings.gains[i]) input.value = settings.gains[i];
    col.querySelector('.value').textContent = fmt(settings.gains[i]);
  });

  $('level').value = settings.level;
  const headroomDb = settings.headroom ? maxBoostDbFor(settings.gains) : 0;
  $('levelValue').textContent = settings.muted ? 'Muted' : fmt(settings.level) + ' dB';
  $('levelValue').title = settings.headroom && headroomDb > 0.05
    ? `Plus ${fmt(-headroomDb)} dB headroom for the EQ's boosts` : '';
  $('headroom').classList.toggle('on', settings.headroom);

  const sel = $('preset');
  const key = settings.preset ? settings.preset : '';
  if (sel.value !== key) sel.value = [...sel.options].some((o) => o.value === key) ? key : '';
  $('delete').disabled = !key.startsWith('u:');
  drawCurve();
}

// ---------------------------------------------------------------- start
async function init() {
  let tab = null;
  if (fixedTab != null) {
    try { tab = await chrome.tabs.get(fixedTab); } catch (_) {}
  } else {
    [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
  }
  if (fromMixer) {
    document.body.classList.add('window');
    for (const n of [16, 32, 48, 128]) {   // keep the mixer's own icon in this window
      const l = document.createElement('link');
      l.rel = 'icon'; l.sizes = `${n}x${n}`; l.href = `assets/mixer${n}.png`;
      document.head.appendChild(l);
    }
    $('mixer').textContent = '‹ Mixer';
    $('mixer').title = 'Back to the mixer';
  }
  tabId = tab ? tab.id : null;
  $('tabName').textContent = tab ? tab.title : '';
  const last = await loadLocal();
  await document.fonts.load('600 18px Barlow').catch(() => {});
  buildBands();
  buildSwatches();
  buildPresetList();

  const state = tabId != null ? await send({ type: 'get', tabId }) : null;
  active = !!(state && state.active);
  // A tab you are already EQ'ing shows its own settings; a new one starts from the last ones you used.
  settings = Object.assign(defaultSettings(), (state && state.settings) || last || {});
  if (!active) status('Off: this tab plays as normal. Turn it on to EQ it.');

  $('mixer').addEventListener('click', async () => {
    if (fromMixer) { location.href = 'mixer.html'; return; }
    await send({ type: 'openMixer' });
    window.close();
  });
  // Follow changes made elsewhere (the mixer, or this tab's popup).
  chrome.storage.onChanged.addListener((changes, area) => {
    if (area === 'local' && changes.profiles) { profiles = changes.profiles.newValue || {}; buildPresetList(); render(); }
    if (area === 'local' && changes.accent) { accent = changes.accent.newValue ?? 0; render(); }
    if (area !== 'session' || !changes.tabs || tabId == null || pushTimer) return;
    const t = (changes.tabs.newValue || {})[tabId];
    const nowActive = !!t;
    if (t && JSON.stringify(t) !== JSON.stringify(settings)) settings = Object.assign(defaultSettings(), t);
    if (nowActive !== active || t) { active = nowActive; render(); }
  });
  $('power').addEventListener('click', () => (active ? stopTab() : startTab()));
  $('level').addEventListener('input', (e) => { settings.level = Number(e.target.value); changed(); });
  $('level').addEventListener('dblclick', () => { settings.level = 0; changed(); });
  $('headroom').addEventListener('click', () => { settings.headroom = !settings.headroom; changed(); });
  $('preset').addEventListener('change', (e) => {
    applyPreset(settings, e.target.value, profiles);
    changed();
  });
  $('save').addEventListener('click', async () => {
    const current = settings.preset.startsWith('u:') ? settings.preset.slice(2) : '';
    const name = (prompt('Name this profile', current || 'My profile') || '').trim();
    if (!name) return;
    profiles[name] = { gains: settings.gains.slice(), level: settings.level, headroom: settings.headroom };
    await saveProfiles();
    settings.preset = 'u:' + name;
    buildPresetList();
    changed();
  });
  $('delete').addEventListener('click', async () => {
    if (!settings.preset.startsWith('u:')) return;
    const name = settings.preset.slice(2);
    if (!confirm(`Delete the profile "${name}"?`)) return;
    delete profiles[name];
    await saveProfiles();
    settings.preset = '';
    buildPresetList();
    changed();
  });
  render();
}

init();
