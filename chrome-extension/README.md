# BEQ Tab EQ (Chrome)

A 10-band EQ and level for each browser tab, in Born To EQ The World's style.
No headphone correction: just tone and volume, per tab.

## Install

1. Download `BEQ-Tab-EQ` from the latest CI run (or use this folder) and unzip it.
2. Open `chrome://extensions`, switch on **Developer mode** (top right).
3. Click **Load unpacked** and pick the unzipped folder.
4. Pin it from the puzzle-piece menu so the icon is always visible.

## Use

- Click the icon on a tab and press **Off** to turn it **On**. Chrome needs that
  click to let the extension hear the tab, and shows its "sharing this tab"
  marker while it runs. The icon shows **EQ** on tabs that are on.
- Each tab keeps its own settings. A tab you turn on starts from the settings
  you used last.
- Drag the bands (double-click for 0 dB) and the level. **Headroom** lowers the
  tab by the EQ's highest boost so boosts can't clip.
- Pick a standard preset, or **Save** your own profiles (kept in Chrome).
- The dots pick the colour, like in the plugin.
- The background menu (at the bottom of the mixer) picks the Forest, one of
  five built-in backgrounds, or **Your own image...**, for the popup and the
  mixer. BEQ picks the colours from it (`theme.js`, the same way as the
  plugin) and adds them as a fifth dot.

## Mixer

**Mixer** in the popup opens a separate window that stays open. It lists every
tab playing sound:

- Tabs that are on get a level slider, a preset menu, **Mute**, and **EQ**
  (the full EQ for that tab, inside the same window).
- Other tabs playing sound can be muted from there. To turn one on, Chrome
  still needs one click on the BEQ icon while you're on that tab (once per page
  load); after that it can be switched on and off from the mixer.
- Click a tab's name to jump to it.
- **Master** turns every tab that is on up or down together. Drag it up or
  down (Shift for fine steps), scroll on it, or double-click for 0 dB.
- **Mute** next to it silences every tab playing sound. Pressing it again
  unmutes only the tabs it muted, not ones you muted yourself.

The mixer needs Chrome's "tabs" permission to show tab titles and icons, so
Chrome lists the extension as able to read your browsing history. Nothing
leaves your computer.
