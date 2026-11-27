// Plays every captured tab through its own EQ and level.
import { buildChain, applySettings } from './eq.js';

let ctx = null;
let master = null;       // master level for every tab, after each tab's own EQ and level
let masterDb = 0;
let masterMute = false;
const tabs = new Map(); // tabId -> { stream, source, chain }

function audio() {
  if (!ctx) {
    ctx = new AudioContext({ latencyHint: 'playback' });
    master = ctx.createGain();
    master.gain.value = masterMute ? 0 : Math.pow(10, masterDb / 20);
    master.connect(ctx.destination);
  }
  return ctx;
}

function setMaster(db, mute, smooth = true) {
  masterDb = db;
  masterMute = !!mute;
  if (!master) return;
  const lin = masterMute ? 0 : Math.pow(10, db / 20);
  if (smooth) master.gain.setTargetAtTime(lin, ctx.currentTime, 0.007); else master.gain.value = lin;
}

async function start(tabId, streamId, settings) {
  stop(tabId);
  const stream = await navigator.mediaDevices.getUserMedia({
    audio: { mandatory: { chromeMediaSource: 'tab', chromeMediaSourceId: streamId } },
    video: false,
  });
  const c = audio();
  const source = c.createMediaStreamSource(stream);
  const chain = buildChain(c);
  applySettings(chain, settings, c, false);
  source.connect(chain.input);
  chain.output.connect(master);
  for (const track of stream.getAudioTracks())
    track.addEventListener('ended', () => {
      stop(tabId);
      chrome.runtime.sendMessage({ target: 'background', type: 'ended', tabId });
    });
  tabs.set(tabId, { stream, source, chain });
  connectPre(tabs.get(tabId));
  if (c.state === 'suspended') await c.resume();
}

// ---------------------------------------------------------------- analyser
// Only while a BEQ window asks for it: the analysers are made on the first request
// and removed again when nothing has asked for a second.
const POINTS = 160; // log-spaced, 20 Hz to 20 kHz
function analyse(t) {
  if (!t.analysers) {
    const make = () => { const a = ctx.createAnalyser(); a.fftSize = 8192; a.smoothingTimeConstant = 0.75; return a; };
    t.analysers = { pre: make(), post: make() };
    t.source.connect(t.analysers.pre);
    t.chain.output.connect(t.analysers.post);
    t.bins = new Float32Array(t.analysers.pre.frequencyBinCount);
  }
  clearTimeout(t.analyserTimer);
  t.analyserTimer = setTimeout(() => dropAnalysers(t), 1000);
  const hz = ctx.sampleRate / t.analysers.pre.fftSize;
  const read = (a) => {
    a.getFloatFrequencyData(t.bins);
    const out = new Array(POINTS);
    for (let i = 0; i < POINTS; i++) {
      // The loudest bin in each slice, so narrow peaks aren't lost at high frequencies.
      const lo = 20 * Math.pow(1000, i / POINTS), hi = 20 * Math.pow(1000, (i + 1) / POINTS);
      let b0 = Math.floor(lo / hz), b1 = Math.max(b0, Math.ceil(hi / hz) - 1), m = -200;
      for (let b = b0; b <= b1 && b < t.bins.length; b++) m = Math.max(m, t.bins[b]);
      out[i] = Math.round(Math.max(-200, m) * 10) / 10;
    }
    return out;
  };
  return { pre: read(t.analysers.pre), post: read(t.analysers.post) };
}
function dropAnalysers(t) {
  if (!t.analysers) return;
  clearTimeout(t.analyserTimer);
  try { t.source.disconnect(t.analysers.pre); } catch (_) {}
  try { t.chain.output.disconnect(t.analysers.post); } catch (_) {}
  t.analysers = null; t.bins = null;
}

// The spectrum window: everything BEQ plays, raw dB per FFT bin, either before any EQ
// (every tab's own sound, summed) or after the master. Made when the window first asks
// and removed a second after it stops asking.
let masterAnalyser = null, masterBins = null, masterTimer = null, masterWhere = null, preMix = null;
function connectPre(t) { if (preMix) t.source.connect(preMix); }
function unhookAnalyser() {
  if (!masterAnalyser) return;
  if (masterWhere === 'pre') {
    for (const t of tabs.values()) { try { t.source.disconnect(preMix); } catch (_) {} }
    try { preMix.disconnect(); } catch (_) {}
    preMix = null;
  } else {
    try { master.disconnect(masterAnalyser); } catch (_) {}
  }
  masterAnalyser = null; masterBins = null; masterWhere = null;
}
function masterSpectrum(fftSize, where) {
  if (!ctx || !master) return null;
  if (masterAnalyser && masterWhere !== where) unhookAnalyser();
  if (!masterAnalyser) {
    masterAnalyser = ctx.createAnalyser();
    masterAnalyser.smoothingTimeConstant = 0; // the window does its own averaging
    masterWhere = where;
    if (where === 'pre') {
      preMix = ctx.createGain(); // only feeds the analyser, never the speakers
      preMix.connect(masterAnalyser);
      for (const t of tabs.values()) connectPre(t);
    } else {
      master.connect(masterAnalyser);
    }
  }
  if (masterAnalyser.fftSize !== fftSize) { masterAnalyser.fftSize = fftSize; masterBins = null; }
  if (!masterBins) masterBins = new Float32Array(masterAnalyser.frequencyBinCount);
  clearTimeout(masterTimer);
  masterTimer = setTimeout(unhookAnalyser, 1000);
  masterAnalyser.getFloatFrequencyData(masterBins);
  const out = new Array(masterBins.length);
  for (let i = 0; i < out.length; i++) out[i] = masterBins[i] < -200 ? -200 : Math.round(masterBins[i] * 100) / 100;
  return { rate: ctx.sampleRate, db: out, playing: tabs.size };
}

function stop(tabId) {
  const t = tabs.get(tabId);
  if (!t) return;
  dropAnalysers(t);
  t.source.disconnect();
  t.chain.output.disconnect();
  t.stream.getTracks().forEach((tr) => tr.stop());
  tabs.delete(tabId);
}

chrome.runtime.onMessage.addListener((msg, sender, reply) => {
  if (msg.target !== 'offscreen') return false;
  if (msg.type === 'master') {
    setMaster(msg.db, msg.mute);
    reply({ ok: true });
    return false;
  }
  if (msg.type === 'start') {
    if (typeof msg.master === 'number') setMaster(msg.master, msg.masterMute, false);
    start(msg.tabId, msg.streamId, msg.settings)
      .then(() => reply({ ok: true }))
      .catch((e) => reply({ ok: false, error: String(e && e.message || e) }));
    return true;
  }
  if (msg.type === 'set') {
    const t = tabs.get(msg.tabId);
    if (t) applySettings(t.chain, msg.settings, audio());
    reply({ ok: !!t });
    return false;
  }
  if (msg.type === 'masterSpectrum') {
    const n = [2048, 4096, 8192, 16384, 32768].includes(msg.fftSize) ? msg.fftSize : 8192;
    reply(masterSpectrum(n, msg.where === 'post' ? 'post' : 'pre'));
    return false;
  }
  if (msg.type === 'spectrum') {
    const t = tabs.get(msg.tabId);
    reply(t && ctx ? analyse(t) : null);
    return false;
  }
  if (msg.type === 'stop') {
    stop(msg.tabId);
    reply({ ok: true });
    return false;
  }
  return false;
});
