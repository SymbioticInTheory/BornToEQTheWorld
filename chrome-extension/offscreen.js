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
  if (c.state === 'suspended') await c.resume();
}

function stop(tabId) {
  const t = tabs.get(tabId);
  if (!t) return;
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
  if (msg.type === 'stop') {
    stop(msg.tabId);
    reply({ ok: true });
    return false;
  }
  return false;
});
