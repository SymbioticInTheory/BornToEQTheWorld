// Keeps track of which tabs are being EQ'd and passes changes to the
// offscreen audio page, where the sound is actually processed.

const OFFSCREEN = 'offscreen.html';

async function ensureOffscreen() {
  const existing = await chrome.runtime.getContexts({ contextTypes: ['OFFSCREEN_DOCUMENT'] });
  if (existing.length > 0) return;
  await chrome.offscreen.createDocument({
    url: OFFSCREEN,
    reasons: ['USER_MEDIA'],
    justification: 'Plays each captured tab through its EQ',
  });
}

async function getTabs() {
  return (await chrome.storage.session.get('tabs')).tabs || {};
}
async function setTabs(tabs) {
  await chrome.storage.session.set({ tabs });
}

async function setBadge(tabId, on) {
  try {
    await chrome.action.setBadgeText({ tabId, text: on ? 'EQ' : '' });
    if (on) await chrome.action.setBadgeBackgroundColor({ tabId, color: '#C42B2B' });
  } catch (_) { /* the tab may be gone */ }
}

async function stopTab(tabId) {
  const tabs = await getTabs();
  if (!tabs[tabId]) return;
  delete tabs[tabId];
  await setTabs(tabs);
  await setBadge(tabId, false);
  try { await chrome.runtime.sendMessage({ target: 'offscreen', type: 'stop', tabId }); } catch (_) {}
  if (Object.keys(tabs).length === 0) {
    try { await chrome.offscreen.closeDocument(); } catch (_) {}
  }
}

async function sendMaster(tabs) {
  if (!Object.keys(tabs).length) return;   // no audio page open
  const { master = 0 } = await chrome.storage.local.get('master');
  const { masterMute = false } = await chrome.storage.session.get('masterMute');
  try { await chrome.runtime.sendMessage({ target: 'offscreen', type: 'master', db: master, mute: masterMute }); } catch (_) {}
}

chrome.runtime.onMessage.addListener((msg, sender, reply) => {
  if (msg.target !== 'background') return false;
  (async () => {
    const tabs = await getTabs();
    switch (msg.type) {
      case 'get':
        reply({ active: !!tabs[msg.tabId], settings: tabs[msg.tabId] || null });
        return;
      case 'start': {
        await ensureOffscreen();
        const { master = 0 } = await chrome.storage.local.get('master');
        const { masterMute = false } = await chrome.storage.session.get('masterMute');
        const r = await chrome.runtime.sendMessage({ target: 'offscreen', type: 'start', tabId: msg.tabId,
                                                     streamId: msg.streamId, settings: msg.settings, master, masterMute });
        if (!r || !r.ok) { reply({ ok: false, error: (r && r.error) || 'Could not start' }); return; }
        tabs[msg.tabId] = msg.settings;
        await setTabs(tabs);
        await setBadge(msg.tabId, true);
        reply({ ok: true });
        return;
      }
      case 'set':
        if (!tabs[msg.tabId]) { reply({ ok: false }); return; }
        tabs[msg.tabId] = msg.settings;
        await setTabs(tabs);
        await chrome.runtime.sendMessage({ target: 'offscreen', type: 'set', tabId: msg.tabId, settings: msg.settings });
        reply({ ok: true });
        return;
      case 'stop':
        await stopTab(msg.tabId);
        reply({ ok: true });
        return;
      case 'master': // the mixer's master knob, for every tab that is on
        await chrome.storage.local.set({ master: msg.db });
        await sendMaster(tabs);
        reply({ ok: true });
        return;
      case 'masterMute': { // silences everything: tabs that are on through BEQ, the rest through Chrome
        const { mutedByMaster = [] } = await chrome.storage.session.get('mutedByMaster');
        if (msg.on) {
          const playing = await chrome.tabs.query({ audible: true });
          for (const t of playing) {
            if (tabs[t.id] || (t.mutedInfo && t.mutedInfo.muted)) continue;
            try { await chrome.tabs.update(t.id, { muted: true }); mutedByMaster.push(t.id); } catch (_) {}
          }
          await chrome.storage.session.set({ masterMute: true, mutedByMaster });
        } else {
          // Unmute only the tabs the master muted, not ones you muted yourself.
          for (const id of mutedByMaster) { try { await chrome.tabs.update(id, { muted: false }); } catch (_) {} }
          await chrome.storage.session.set({ masterMute: false, mutedByMaster: [] });
        }
        await sendMaster(tabs);
        reply({ ok: true });
        return;
      }
      case 'openMixer': {
        // One mixer window: bring it forward if it is already open.
        const { mixerWindow } = await chrome.storage.session.get('mixerWindow');
        if (mixerWindow != null) {
          try { await chrome.windows.update(mixerWindow, { focused: true }); reply({ ok: true }); return; } catch (_) {}
        }
        const { mixerScale = 1 } = await chrome.storage.local.get('mixerScale');
        const w = await chrome.windows.create({ url: 'mixer.html', type: 'popup',
          width: Math.round(500 * mixerScale), height: Math.round(720 * mixerScale) });
        await chrome.storage.session.set({ mixerWindow: w.id });
        reply({ ok: true });
        return;
      }
      case 'ended': // the tab's stream stopped by itself (tab closed, capture ended)
        await stopTab(msg.tabId);
        reply({ ok: true });
        return;
    }
    reply({ ok: false });
  })();
  return true; // reply comes asynchronously
});

chrome.tabs.onRemoved.addListener((tabId) => { stopTab(tabId); });
chrome.windows.onRemoved.addListener(async (id) => {
  const { mixerWindow } = await chrome.storage.session.get('mixerWindow');
  if (mixerWindow === id) await chrome.storage.session.remove('mixerWindow');
});
