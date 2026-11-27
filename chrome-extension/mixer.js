// The mixer window: every tab playing sound in one place, with a level for
// each tab that is EQ'd and a mute for the rest.
import { PRESETS, defaultSettings, applyPreset } from './eq.js';

const ACCENTS = ['#C42B2B', '#E46A2E', '#D9D5CD', '#7D78D6'];
const ACCENT_NAMES = ['Blood red', 'Ember', 'Bone', 'Frost'];
const $ = (id) => document.getElementById(id);
const send = (msg) => chrome.runtime.sendMessage({ target: 'background', ...msg });

let eqTabs = {};      // tabId -> settings, for tabs being EQ'd
let profiles = {};
let accent = 0;
let lastSettings = null;
const strips = new Map(); // tabId -> strip element
const notes = new Map();  // tabId -> note to show under the strip
const pending = new Map(); // tabId -> timer for throttled changes

function fmt(db) { return (db > 0 ? '+' : '') + db.toFixed(1); }

// ---------------------------------------------------------------- changes
function push(tabId) {
  if (pending.has(tabId)) return;
  pending.set(tabId, setTimeout(() => {
    pending.delete(tabId);
    if (eqTabs[tabId]) send({ type: 'set', tabId, settings: eqTabs[tabId] });
  }, 30));
}

async function turnOn(tab) {
  try {
    const streamId = await chrome.tabCapture.getMediaStreamId({ targetTabId: tab.id });
    const settings = Object.assign(defaultSettings(), lastSettings || {});
    settings.muted = false;
    const r = await send({ type: 'start', tabId: tab.id, streamId, settings });
    if (!r || !r.ok) throw new Error(r && r.error);
    notes.delete(tab.id);
  } catch (e) {
    // Chrome only lets BEQ take a tab's sound after you click its icon on that tab.
    if (/invoked/.test(String(e && e.message))) {
      notes.set(tab.id, { warn: true, text: 'Chrome needs one click on the BEQ icon on this tab first. Taking you there: click the icon, then turn it on.' });
      await focusTab(tab);
    } else {
      notes.set(tab.id, { warn: true, text: "This tab can't be EQ'd" + (e && e.message ? ': ' + e.message : '.') });
    }
  }
  refresh();
}

async function focusTab(tab) {
  await chrome.tabs.update(tab.id, { active: true });
  await chrome.windows.update(tab.windowId, { focused: true });
}

// ---------------------------------------------------------------- strips
function presetOptions(sel) {
  sel.innerHTML = '';
  const add = (parent, value, label) => {
    const o = document.createElement('option');
    o.value = value; o.textContent = label; parent.appendChild(o);
  };
  add(sel, '', 'Custom');
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

function makeStrip(tabId) {
  const el = document.createElement('section');
  el.className = 'panel strip';
  el.innerHTML = `
    <div class="top">
      <img class="icon" alt="">
      <span class="name" title="Go to this tab"></span>
      <button class="pill mute" title="Mute this tab">Mute</button>
      <button class="pill edit" title="Edit this tab's EQ">EQ</button>
      <button class="pill power" title="EQ this tab on or off">Off</button>
    </div>
    <div class="controls">
      <span class="label">Level</span>
      <input type="range" min="-40" max="12" step="0.5" value="0" title="Double-click for 0 dB">
      <span class="value"></span>
      <select title="Preset or profile"></select>
    </div>
    <div class="note"></div>`;
  const level = el.querySelector('input'), sel = el.querySelector('select');
  el.querySelector('.icon').addEventListener('error', (e) => { e.target.hidden = true; });
  el.querySelector('.name').addEventListener('click', () => focusTab(el.tab));
  el.querySelector('.mute').addEventListener('click', async () => {
    const s = eqTabs[tabId];
    if (s) { s.muted = !s.muted; push(tabId); update(el, el.tab); }   // EQ'd: mute BEQ's output
    else await chrome.tabs.update(tabId, { muted: !(el.tab.mutedInfo && el.tab.mutedInfo.muted) });
  });
  el.querySelector('.edit').addEventListener('click', () => {
    location.href = `popup.html?tab=${tabId}&from=mixer`;
  });
  el.querySelector('.power').addEventListener('click', async () => {
    if (eqTabs[tabId]) await send({ type: 'stop', tabId });
    else await turnOn(el.tab);
  });
  const setLevel = (v) => {
    const s = eqTabs[tabId];
    if (!s) return;
    s.level = v;
    push(tabId);
    update(el, el.tab);
  };
  level.addEventListener('input', () => setLevel(Number(level.value)));
  level.addEventListener('dblclick', () => setLevel(0));
  sel.addEventListener('change', () => {
    const s = eqTabs[tabId];
    if (!s || !sel.value) return;
    applyPreset(s, sel.value, profiles);
    push(tabId);
    update(el, el.tab);
  });
  presetOptions(sel);
  return el;
}

function update(el, tab) {
  el.tab = tab;
  const s = eqTabs[tab.id];
  const muted = s ? !!s.muted : !!(tab.mutedInfo && tab.mutedInfo.muted);
  el.classList.toggle('off', !s);
  el.classList.toggle('muted', muted);
  const icon = el.querySelector('.icon');
  if (tab.favIconUrl && icon.getAttribute('src') !== tab.favIconUrl) { icon.hidden = false; icon.src = tab.favIconUrl; }
  icon.hidden = !tab.favIconUrl;
  el.querySelector('.name').textContent = tab.title || tab.url || 'Tab';
  const mute = el.querySelector('.mute');
  mute.classList.toggle('on', muted);
  mute.textContent = muted ? 'Muted' : 'Mute';
  const power = el.querySelector('.power');
  power.classList.toggle('on', !!s);
  power.textContent = s ? 'On' : 'Off';
  el.querySelector('.edit').hidden = !s;
  el.querySelector('.controls').hidden = !s;
  if (s) {
    const level = el.querySelector('input');
    if (document.activeElement !== level || Number(level.value) !== s.level) level.value = s.level;
    el.querySelector('.value').textContent = fmt(s.level) + ' dB';
    const sel = el.querySelector('select');
    const key = [...sel.options].some((o) => o.value === s.preset) ? s.preset : '';
    if (sel.value !== key) sel.value = key;
  }
  const note = notes.get(tab.id);
  const n = el.querySelector('.note');
  n.hidden = !note && !!s;
  n.className = 'note' + (note && note.warn ? ' warn' : '');
  n.textContent = note ? note.text : (s ? '' : 'Playing. Turn it on to set its level and EQ.');
}

// ---------------------------------------------------------------- list
let refreshing = null;
function refresh() {
  if (!refreshing) refreshing = Promise.resolve().then(doRefresh).finally(() => { refreshing = null; });
  return refreshing;
}

async function doRefresh() {
  eqTabs = (await chrome.storage.session.get('tabs')).tabs || {};
  const all = await chrome.tabs.query({});
  // EQ'd tabs first, then the others playing sound.
  const own = chrome.runtime.getURL('');
  const shown = all.filter((t) => !(t.url || '').startsWith(own) && (eqTabs[t.id] || t.audible || notes.has(t.id)))
    .sort((a, b) => (!!eqTabs[b.id] - !!eqTabs[a.id]) || a.id - b.id);
  const box = $('rows');
  const keep = new Set(shown.map((t) => t.id));
  for (const [id, el] of strips) if (!keep.has(id)) { el.remove(); strips.delete(id); }
  shown.forEach((tab, i) => {
    let el = strips.get(tab.id);
    if (!el) { el = makeStrip(tab.id); strips.set(tab.id, el); }
    if (box.children[i] !== el) box.insertBefore(el, box.children[i] || null);
    update(el, tab);
  });
  $('empty').hidden = shown.length > 0;
}

// ---------------------------------------------------------------- master knob
const MASTER_MIN = -30, MASTER_MAX = 6;
let masterDb = 0;
let masterTimer = null;

function knobPoint(a, r) {   // angle in degrees, 0 = straight up
  const t = (a - 90) * Math.PI / 180;
  return [24 + r * Math.cos(t), 24 + r * Math.sin(t)];
}
function arc(a0, a1, r) {
  const [x0, y0] = knobPoint(a0, r), [x1, y1] = knobPoint(a1, r);
  return `M ${x0} ${y0} A ${r} ${r} 0 ${Math.abs(a1 - a0) > 180 ? 1 : 0} ${a1 > a0 ? 1 : 0} ${x1} ${y1}`;
}
const angleFor = (db) => -135 + 270 * (db - MASTER_MIN) / (MASTER_MAX - MASTER_MIN);

function drawKnob() {
  const a = angleFor(masterDb), zero = angleFor(0), r = 21;
  $('knobTrack').setAttribute('d', arc(-135, 135, r));
  $('knobArc').setAttribute('d', Math.abs(a - zero) < 0.5 ? '' : arc(Math.min(a, zero), Math.max(a, zero), r));
  const [x, y] = knobPoint(a, 12);
  $('knobPointer').setAttribute('x2', x);
  $('knobPointer').setAttribute('y2', y);
  $('masterValue').textContent = fmt(masterDb) + ' dB';
}

function setMasterDb(db) {
  masterDb = Math.round(Math.min(MASTER_MAX, Math.max(MASTER_MIN, db)) * 2) / 2;
  drawKnob();
  if (masterTimer) return;
  masterTimer = setTimeout(() => { masterTimer = null; send({ type: 'master', db: masterDb }); }, 30);
}

function buildKnob() {
  const knob = $('knob');
  let startY = 0, startDb = 0;
  const move = (e) => {
    const per = e.shiftKey ? 0.05 : 0.25;   // dB per pixel; Shift for fine steps
    setMasterDb(startDb + (startY - e.clientY) * per);
  };
  knob.addEventListener('pointerdown', (e) => {
    startY = e.clientY; startDb = masterDb;
    knob.setPointerCapture(e.pointerId);
    knob.addEventListener('pointermove', move);
  });
  const end = () => knob.removeEventListener('pointermove', move);
  knob.addEventListener('pointerup', end);
  knob.addEventListener('pointercancel', end);
  knob.addEventListener('dblclick', () => setMasterDb(0));
  knob.addEventListener('wheel', (e) => { e.preventDefault(); setMasterDb(masterDb + (e.deltaY < 0 ? 0.5 : -0.5)); }, { passive: false });
  knob.addEventListener('keydown', (e) => {
    if (e.key === 'ArrowUp' || e.key === 'ArrowRight') setMasterDb(masterDb + 0.5);
    else if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') setMasterDb(masterDb - 0.5);
    else if (e.key === '0') setMasterDb(0);
    else return;
    e.preventDefault();
  });
  drawKnob();
}

// ---------------------------------------------------------------- theme
function buildSwatches() {
  const box = $('swatches');
  ACCENTS.forEach((c, i) => {
    const b = document.createElement('button');
    b.className = 'swatch';
    b.title = ACCENT_NAMES[i];
    b.innerHTML = `<span style="background:${c}"></span>`;
    b.addEventListener('click', () => chrome.storage.local.set({ accent: i }));
    box.appendChild(b);
  });
}

function applyTheme() {
  document.documentElement.style.setProperty('--accent', ACCENTS[accent]);
  $('title').src = `assets/title${accent}.png`;
  [...$('swatches').children].forEach((b, i) => b.classList.toggle('on', i === accent));
}

// ---------------------------------------------------------------- start
async function init() {
  const s = await chrome.storage.local.get(['profiles', 'accent', 'lastSettings', 'master']);
  masterDb = s.master ?? 0;
  profiles = s.profiles || {};
  accent = s.accent ?? 0;
  lastSettings = s.lastSettings || null;
  buildSwatches();
  buildKnob();
  applyTheme();

  chrome.storage.onChanged.addListener((changes, area) => {
    if (area === 'local') {
      if (changes.accent) { accent = changes.accent.newValue ?? 0; applyTheme(); }
      if (changes.lastSettings) lastSettings = changes.lastSettings.newValue || null;
      if (changes.profiles) {
        profiles = changes.profiles.newValue || {};
        for (const el of strips.values()) presetOptions(el.querySelector('select'));
        refresh();
      }
    }
    if (area === 'session' && changes.tabs && pending.size === 0) {
      for (const id of Object.keys(changes.tabs.newValue || {})) notes.delete(Number(id));
      refresh();
    }
  });
  chrome.tabs.onUpdated.addListener((id, info) => {
    if ('audible' in info || 'title' in info || 'mutedInfo' in info || 'favIconUrl' in info) refresh();
  });
  chrome.tabs.onRemoved.addListener((id) => { notes.delete(id); refresh(); });
  chrome.tabs.onActivated.addListener(() => refresh());
  await refresh();
}

init();
