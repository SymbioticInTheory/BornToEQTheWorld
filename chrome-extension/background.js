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
        const r = await chrome.runtime.sendMessage({ target: 'offscreen', type: 'start', tabId: msg.tabId,
                                                     streamId: msg.streamId, settings: msg.settings });
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
