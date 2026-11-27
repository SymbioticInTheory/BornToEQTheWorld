// BEQ's colours for the popup and the mixer: four built-in accents, and a
// fifth picked from your own background image. The colour picking matches
// src/plugin/ImageTheme.h in the plugin; keep the two in step.

export const ACCENTS = ['#C42B2B', '#E46A2E', '#D9D5CD', '#7D78D6'];
export const ACCENT_NAMES = ['Blood red', 'Ember', 'Bone', 'Frost', 'From your image'];
export const IMAGE_ACCENT = 4;
const FOREST_FOG = '#0A0B0C';

// ---------------------------------------------------------------- colour maths
const clamp01 = (v) => Math.min(1, Math.max(0, v));

function toHsv(r, g, b) {
  const mx = Math.max(r, g, b), mn = Math.min(r, g, b), d = mx - mn;
  let h = 0;
  if (d > 0) {
    if (mx === r) h = (((g - b) / d) % 6 + 6) % 6;
    else if (mx === g) h = (b - r) / d + 2;
    else h = (r - g) / d + 4;
    h *= 60;
  }
  return [h, mx > 0 ? d / mx : 0, mx];
}

function toHsl(r, g, b) {
  const [h] = toHsv(r, g, b);
  const mx = Math.max(r, g, b), mn = Math.min(r, g, b), l = (mx + mn) / 2;
  const s = mx === mn ? 0 : (mx - mn) / (1 - Math.abs(2 * l - 1));
  return [h, s, l];
}

function fromHsl(h, s, l) {
  h = ((h % 360) + 360) % 360;
  const c = (1 - Math.abs(2 * l - 1)) * s, x = c * (1 - Math.abs(((h / 60) % 2) - 1)), m = l - c / 2;
  let r = 0, g = 0, b = 0;
  if (h < 60) [r, g] = [c, x];
  else if (h < 120) [r, g] = [x, c];
  else if (h < 180) [g, b] = [c, x];
  else if (h < 240) [g, b] = [x, c];
  else if (h < 300) [r, b] = [x, c];
  else [r, b] = [c, x];
  return [clamp01(r + m), clamp01(g + m), clamp01(b + m)];
}

function luminance([r, g, b]) {
  const lin = (v) => (v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4));
  return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b);
}

const hueDistance = (a, b) => Math.abs((((a - b + 540) % 360) + 360) % 360 - 180);
const fromHex = (v) => [((v >> 16) & 255) / 255, ((v >> 8) & 255) / 255, (v & 255) / 255];
const toHex = ([r, g, b]) => '#' + [r, g, b].map((v) => Math.round(v * 255).toString(16).padStart(2, '0')).join('').toUpperCase();

function furthest(candidates, avoid, enough = 75) {
  let best = candidates[0], bestScore = -1;
  for (const c of candidates) {
    const [h] = toHsv(...c);
    let score = 360;
    for (const a of avoid) score = Math.min(score, hueDistance(h, a));
    if (score >= enough) return c;
    if (score > bestScore + 1e-9) { bestScore = score; best = c; }
  }
  return best;
}

// `rgb`: R, G, B bytes (from a small copy of the image). Returns hex colours.
export function pickColours(rgb) {
  const bins = 36;
  const hist = new Float64Array(bins), cosSum = new Float64Array(bins), sinSum = new Float64Array(bins), satSum = new Float64Array(bins);
  let vividTotal = 0, dR = 0, dG = 0, dB = 0, dN = 0, aR = 0, aG = 0, aB = 0;
  const n = Math.floor(rgb.length / 3);
  for (let i = 0; i < n; i++) {
    const r = rgb[i * 3] / 255, g = rgb[i * 3 + 1] / 255, b = rgb[i * 3 + 2] / 255;
    const [h, s, v] = toHsv(r, g, b);
    aR += r; aG += g; aB += b;
    if (v < 0.4) { dR += r; dG += g; dB += b; dN++; }
    if (s < 0.2 || v < 0.25) continue;
    const w = s * s * v * v; // favours strong, bright colour over large dull areas
    const bin = Math.min(bins - 1, Math.floor(h / (360 / bins)));
    const rad = (h * Math.PI) / 180;
    hist[bin] += w; cosSum[bin] += Math.cos(rad) * w; sinSum[bin] += Math.sin(rad) * w; satSum[bin] += s * w;
    vividTotal += w;
  }
  const out = { vivid: true };
  const dark = dN > 0 ? [dR / dN, dG / dN, dB / dN] : n > 0 ? [aR / n, aG / n, aB / n] : [0, 0, 0];
  { const [h, s] = toHsl(...dark); out.fog = fromHsl(h, Math.min(s, 0.35), 0.045); }

  let accentHue;
  if (n === 0 || vividTotal / n < 0.003) {
    out.vivid = false;
    const [h, s] = toHsl(...(n > 0 ? [aR / n, aG / n, aB / n] : [0, 0, 0]));
    accentHue = h;
    out.accent = fromHsl(h, Math.min(s, 0.1), 0.84);
  } else {
    let peak = 0, best = -1;
    for (let b = 0; b < bins; b++) {
      const score = hist[b] + 0.5 * (hist[(b + bins - 1) % bins] + hist[(b + 1) % bins]);
      if (score > best) { best = score; peak = b; }
    }
    let cs = 0, sn = 0, sat = 0, w = 0;
    for (let d = -1; d <= 1; d++) {
      const b = (peak + d + bins) % bins;
      cs += cosSum[b]; sn += sinSum[b]; sat += satSum[b]; w += hist[b];
    }
    accentHue = ((Math.atan2(sn, cs) * 180) / Math.PI + 360) % 360;
    const s = Math.min(0.85, Math.max(0.5, sat / w));
    let l = 0.52;
    out.accent = fromHsl(accentHue, s, l);
    while (luminance(out.accent) < 0.16 && l < 0.75) { l += 0.01; out.accent = fromHsl(accentHue, s, l); }
  }
  const avoid = [43];
  if (out.vivid) avoid.push(accentHue);
  out.eq = furthest([fromHex(0x3CBFA4), fromHex(0xD2607E), fromHex(0xB4A6EC)], avoid);
  avoid.push(toHsv(...out.eq)[0]);
  out.measured = out.vivid ? furthest([fromHex(0x6F8296), fromHex(0x4F86D6), fromHex(0x9A9EA4)], avoid, 40) : fromHex(0x4F86D6);
  return { accent: toHex(out.accent), fog: toHex(out.fog), eq: toHex(out.eq), measured: toHex(out.measured), vivid: out.vivid };
}

// ---------------------------------------------------------------- your image
// Reads an image file, keeps a copy of at most 1600 px (so it fits in Chrome's
// storage) and picks its colours.
export async function imageFromFile(file) {
  const bmp = await createImageBitmap(file);
  const fit = Math.min(1, 1600 / Math.max(bmp.width, bmp.height));
  const w = Math.max(1, Math.round(bmp.width * fit)), h = Math.max(1, Math.round(bmp.height * fit));
  const big = new OffscreenCanvas(w, h);
  big.getContext('2d').drawImage(bmp, 0, 0, w, h);
  const blob = await big.convertToBlob({ type: 'image/jpeg', quality: 0.85 });
  const dataUrl = await new Promise((res, rej) => {
    const fr = new FileReader();
    fr.onload = () => res(fr.result);
    fr.onerror = () => rej(fr.error);
    fr.readAsDataURL(blob);
  });
  const sw = 96, sh = Math.max(1, Math.round((96 * h) / w));
  const small = new OffscreenCanvas(sw, sh);
  const sg = small.getContext('2d');
  sg.drawImage(bmp, 0, 0, sw, sh);
  const px = sg.getImageData(0, 0, sw, sh).data, rgb = new Uint8Array(sw * sh * 3);
  for (let i = 0, j = 0; i < px.length; i += 4) { rgb[j++] = px[i]; rgb[j++] = px[i + 1]; rgb[j++] = px[i + 2]; }
  return { dataUrl, colours: pickColours(rgb) };
}

// ---------------------------------------------------------------- applying it
export async function loadTheme() {
  const s = await chrome.storage.local.get(['accent', 'bgImage', 'imageColours', 'bgName']);
  const image = s.bgImage && s.imageColours ? { dataUrl: s.bgImage, colours: s.imageColours, name: s.bgName || 'custom' } : null;
  let accent = s.accent ?? 0;
  if (accent === IMAGE_ACCENT && !image) accent = 0;
  return { accent, image };
}

export const accentColour = (theme, i = theme.accent) =>
  i === IMAGE_ACCENT && theme.image ? theme.image.colours.accent : ACCENTS[Math.min(i, ACCENTS.length - 1)];

// The Death-style title in any colour: the Blood red version re-coloured,
// keeping its shading (each pixel's brightness relative to the red it was drawn in).
const titleCache = new Map();
async function tintedTitle(hex) {
  if (titleCache.has(hex)) return titleCache.get(hex);
  const img = new Image();
  img.src = 'assets/title0.png';
  await img.decode();
  const c = document.createElement('canvas');
  c.width = img.naturalWidth; c.height = img.naturalHeight;
  const g = c.getContext('2d');
  g.drawImage(img, 0, 0);
  const data = g.getImageData(0, 0, c.width, c.height), px = data.data;
  const v = parseInt(hex.slice(1), 16), ar = (v >> 16) & 255, ag = (v >> 8) & 255, ab = v & 255;
  const lum = (r, gg, b) => 0.299 * r + 0.587 * gg + 0.114 * b;
  const base = lum(0xC4, 0x2B, 0x2B);
  for (let i = 0; i < px.length; i += 4) {
    if (px[i + 3] === 0) continue;
    const f = lum(px[i], px[i + 1], px[i + 2]) / base;   // 1 = the plain red
    let r = ar * f, gg = ag * f, b = ab * f;
    if (f > 1) {                                          // highlights: towards white, like the original
      const t = Math.min(1, (lum(px[i], px[i + 1], px[i + 2]) - base) / Math.max(1, 255 - base));
      r = ar + (255 - ar) * t; gg = ag + (255 - ag) * t; b = ab + (255 - ab) * t;
    }
    px[i] = Math.min(255, r); px[i + 1] = Math.min(255, gg); px[i + 2] = Math.min(255, b);
  }
  g.putImageData(data, 0, 0);
  const url = c.toDataURL();
  titleCache.set(hex, url);
  return url;
}

export async function applyTheme(theme, titleEl, swatchBox) {
  const root = document.documentElement.style;
  root.setProperty('--accent', accentColour(theme));
  root.setProperty('--fog', theme.image ? theme.image.colours.fog : FOREST_FOG);
  root.setProperty('--bg-image', theme.image ? `url("${theme.image.dataUrl}")` : 'url(assets/forest.jpg)');
  if (titleEl) {
    titleEl.src = theme.accent === IMAGE_ACCENT && theme.image
      ? await tintedTitle(theme.image.colours.accent) : `assets/title${theme.accent}.png`;
  }
  if (swatchBox) {
    [...swatchBox.children].forEach((b, i) => {
      b.classList.toggle('on', i === theme.accent);
      if (i === IMAGE_ACCENT) {
        b.hidden = !theme.image;
        if (theme.image) b.querySelector('span').style.background = theme.image.colours.accent;
      }
    });
  }
}

export function buildSwatches(box, onPick) {
  for (let i = 0; i <= IMAGE_ACCENT; i++) {
    const b = document.createElement('button');
    b.className = 'swatch';
    b.title = ACCENT_NAMES[i];
    b.innerHTML = `<span style="background:${ACCENTS[i] || 'transparent'}"></span>`;
    b.addEventListener('click', () => onPick(i));
    box.appendChild(b);
  }
}
