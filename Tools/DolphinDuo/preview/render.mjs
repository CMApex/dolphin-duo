// SPDX-License-Identifier: GPL-2.0-or-later
//
// Renders a Dolphin Duo canvas snapshot to a PNG with headless Chromium (Playwright).
//
//   dolphin-tool duo preview my-companion --mem1 mem1.raw --screen 1240x1080 --json > snap.json
//   node Tools/DolphinDuo/preview/render.mjs snap.json out.png [--size 1240x1080]
//
// --size is the screen in pixels (default: the layout's canvas size). Fonts and images are loaded
// from the companion folder named in the snapshot.

import { existsSync, readFileSync } from 'node:fs';
import { dirname, extname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { createRequire } from 'node:module';

// Resolved like require() so a global install (NODE_PATH) works too.
const { chromium } = createRequire(import.meta.url)('playwright');

const here = dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
if (args.length < 2) {
  console.error('usage: render.mjs <snapshot.json> <out.png> [--size WxH]');
  process.exit(1);
}
const snapshot = JSON.parse(readFileSync(args[0], 'utf8'));
if (!snapshot.canvas) {
  console.error('This snapshot has no canvas page (the companion has no layouts, or the page has no elements).');
  process.exit(1);
}
let width = snapshot.canvas.w;
let height = snapshot.canvas.h;
const sizeIndex = args.indexOf('--size');
if (sizeIndex >= 0) [width, height] = args[sizeIndex + 1].split('x').map(Number);

const base = snapshot.companion && snapshot.companion.directory ? pathToFileURL(snapshot.companion.directory).href.replace(/\/?$/, '/') : '';
const script = readFileSync(join(here, 'duo-preview.js'), 'utf8');

const browser = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH || undefined, args: ['--allow-file-access-from-files'] });
const page = await browser.newPage({ viewport: { width, height }, deviceScaleFactor: 1 });
page.on('console', (msg) => console.error('[page]', msg.text()));
await page.setContent(`<!doctype html><html><body style="margin:0;background:#000"><canvas id="c" width="${width}" height="${height}"></canvas></body></html>`);
await page.addScriptTag({ content: script });
// Pages opened from setContent cannot read file:// URLs, so inline fonts and images.
const directory = snapshot.companion && snapshot.companion.directory;
const wanted = new Set(Object.values(snapshot.canvas.fonts || {}));
for (const e of snapshot.elements || []) if (e.t === 'image' && e.src) wanted.add(e.src);
const types = { '.ttf': 'font/ttf', '.otf': 'font/otf', '.png': 'image/png', '.jpg': 'image/jpeg', '.webp': 'image/webp' };
const inlined = {};
for (const path of wanted) {
  const file = directory ? join(directory, path) : null;
  if (file && existsSync(file)) {
    inlined[path] = `data:${types[extname(path).toLowerCase()] || 'application/octet-stream'};base64,${readFileSync(file).toString('base64')}`;
  }
}
await page.evaluate(async ({ snapshot, inlined, base }) => {
  const assets = await window.DuoPreview.loadAssets(snapshot, (path) => inlined[path] || base + path);
  window.DuoPreview.render(document.getElementById('c'), snapshot, assets);
}, { snapshot, inlined, base });
await page.locator('#c').screenshot({ path: args[1] });
await browser.close();
console.log(`wrote ${args[1]} (${width}x${height}, layout ${snapshot.canvas.layout})`);
