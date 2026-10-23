// captures the website section by section as smooth scrolls (one screenshot per video frame), CDNs served locally
import { chromium } from 'playwright-core';
import fs from 'fs';
const NM = new URL('./node_modules/', import.meta.url).pathname;	// three@0.170.0, gsap@3.12.5 (npm install)
const [,, out, json] = process.argv;	// scenes: [[name, fromSelector/offset, toSelector/offset, frames], ...]
const scenes = JSON.parse(json);
const b = await chromium.launch({ executablePath: '/opt/pw-browsers/chromium-1194/chrome-linux/chrome', ignoreDefaultArgs: ['--headless=old'],
  args: ['--headless=new', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
// DSF: device pixels per CSS pixel; under 1 the page keeps its 1440 x 900 layout but is drawn smaller (faster WebGL)
const p = await b.newPage({ viewport: { width: 1440, height: 900 }, deviceScaleFactor: +(process.env.DSF || 1) });
p.on('pageerror', e => console.log('error:', e.message));
await p.route('https://cdnjs.cloudflare.com/ajax/libs/gsap/3.12.5/**', r => r.fulfill({ path: NM + 'gsap/dist/' + r.request().url().split('/').pop(), contentType: 'text/javascript' }));
await p.route('https://cdn.jsdelivr.net/npm/three@0.170.0/**', r => r.fulfill({ path: NM + 'three/' + r.request().url().split('three@0.170.0/')[1], contentType: 'text/javascript' }));
await p.route('https://api.github.com/**', r => r.fulfill({ body: '[]', contentType: 'application/json' }));
await p.goto('http://127.0.0.1:8766/index.html');
// GSAP's lag smoothing slows its clock when frames are slow (software WebGL): the intro and the reveals would never
// finish; on wall-clock time they do
await p.waitForFunction(() => window.gsap, null, { timeout: 120000 });
await p.evaluate(() => window.gsap.ticker.lagSmoothing(0));
await p.waitForFunction(() => document.getElementById('loader').classList.contains('done'), null, { timeout: 180000 });
await p.waitForTimeout(5000);
const pos = async s => typeof s === 'number' ? s : await p.evaluate(sel => { const e = document.querySelector(sel); return e ? e.getBoundingClientRect().top + scrollY : 0; }, s);
const ease0 = t => t;
const ease = t => t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2;
for (const [name, from, to, frames, dFrom = 0, dTo = 0, start = 0] of scenes) {
  fs.mkdirSync(`${out}/${name}`, { recursive: true });
  const a = await pos(from) + dFrom, z = await pos(to) + dTo;
  // resume: start at the first frame not captured yet (scrolled there first, so the scrubbed pose has caught up)
  // start: a later first frame (another worker takes the scene's first part)
  let f0 = start;
  while (f0 < frames && fs.existsSync(`${out}/${name}/f${String(f0).padStart(4, '0')}.jpg`)) f0++;
  if (f0 >= frames) continue;
  await p.evaluate(y => window.scrollTo(0, y), a + (z - a) * ease(f0 / Math.max(1, frames - 1))); await p.waitForTimeout(3000);
  const t0 = Date.now();
  for (let f = f0; f < frames; f++) {
    // a frame already there (another worker took the rest of this scene): this worker's part is done
    if (f > f0 && fs.existsSync(`${out}/${name}/f${String(f).padStart(4, '0')}.jpg`)) break;
    await p.evaluate(y => window.scrollTo(0, y), a + (z - a) * ease(f / Math.max(1, frames - 1)));
    await p.evaluate(() => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r))));
    await p.screenshot({ path: `${out}/${name}/f${String(f).padStart(4, '0')}.jpg`, quality: 92, timeout: 180000 });
  }
  console.log(name, frames, 'frames', ((Date.now() - t0) / frames).toFixed(0), 'ms/frame');
}
await b.close();
