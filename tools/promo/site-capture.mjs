// captures the website along one scroll path on a virtual clock: the page's time (performance.now, Date.now and
// requestAnimationFrame) advances exactly 1/30 s per captured frame, however long software WebGL takes to draw it,
// so its slideshow, idle motion and scrubbed poses run at their real pace in the video.
//   node capture2.mjs OUT '<path json>' first last       path: [[name, frames, y0, y1], ...] (the whole video's path);
//   captures the path's scenes first..last (indices), fast-forwarding through the ones before so the page is in the
//   state it would have reached
import { chromium } from 'playwright-core';
import fs from 'fs';
const NM = new URL('./node_modules/', import.meta.url).pathname;	// three@0.170.0, gsap@3.12.5 (npm install)
const SITE = process.env.SITE_URL || 'http://127.0.0.1:8766/';	// web/preview served here
const [,, out, pathJson, first, last] = process.argv;
const PATH = JSON.parse(pathJson);
const FPS = 30, STEP = 1000 / FPS;
const b = await chromium.launch({ executablePath: '/opt/pw-browsers/chromium-1194/chrome-linux/chrome', ignoreDefaultArgs: ['--headless=old'],
  args: ['--headless=new', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
// DSF: device pixels per CSS pixel; under 1 the page keeps its 1440 x 900 layout but is drawn smaller (faster WebGL)
const p = await b.newPage({ viewport: { width: 1440, height: 900 }, deviceScaleFactor: +(process.env.DSF || 1) });
p.on('pageerror', e => console.log('error:', e.message));
// the virtual clock, before any of the page's scripts run
await p.addInitScript(() => {
  let T = 0, id = 0;
  const base = Date.now(), queue = new Map();
  performance.now = () => T;
  Date.now = () => base + T;
  window.requestAnimationFrame = cb => { queue.set(++id, cb); return id; };
  window.cancelAnimationFrame = i => queue.delete(i);
  window.__step = ms => {
    T += ms;
    const q = [...queue.values()]; queue.clear();
    for (const f of q) { try { f(T); } catch (e) { console.error(e); } }
  };
});
await p.route('https://cdnjs.cloudflare.com/ajax/libs/gsap/3.12.5/**', r => r.fulfill({ path: NM + 'gsap/dist/' + r.request().url().split('/').pop(), contentType: 'text/javascript' }));
await p.route('https://cdn.jsdelivr.net/npm/three@0.170.0/**', r => r.fulfill({ path: NM + 'three/' + r.request().url().split('three@0.170.0/')[1], contentType: 'text/javascript' }));
await p.route('https://api.github.com/**', r => r.fulfill({ body: '[]', contentType: 'application/json' }));
// for the capture only: no automatic quality drop (every frame is "slow" here), and the slideshow counts each step's
// whole time (so a fast-forward in long steps keeps its pace)
await p.route('**/js/main.js', async r => {
  const res = await r.fetch();
  let js = await res.text();
  const n0 = js.length;
  js = js.replace('if (quality.frames >= 90)', 'if (false && quality.frames >= 90)').replace('stepSlides(dt);', 'stepSlides(raw);');
  if (js.length === n0) console.log('warning: main.js not patched');
  r.fulfill({ response: res, body: js, contentType: 'text/javascript' });
});
await p.goto(SITE + 'index.html');
// loading: the clock runs while the model loads (the loader's own animation needs it)
const realWait = ms => new Promise(r => setTimeout(r, ms));
for (let k = 0; k < 600 && !(await p.evaluate(() => document.getElementById('loader').classList.contains('done'))); k++) {
  await p.evaluate(() => window.__step(16));
  await realWait(200);
}
// the intro (lid, cards, then the scroll timeline is made): 4 s, in 0.1 s steps
for (let k = 0; k < 40; k++) await p.evaluate(() => window.__step(100));
const ease = t => t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2;
// the scroll, at once (the site's CSS has scroll-behavior: smooth, which would animate it on real time), and
// ScrollTrigger told of it at once (its scroll event comes on the browser's own schedule)
const scrollTo = y => p.evaluate(y => { window.scrollTo({ top: y, behavior: 'instant' }); if (window.ScrollTrigger) window.ScrollTrigger.update(); }, y);
for (let si = 0; si <= +last; si++) {
  const [name, frames, y0, y1] = PATH[si];
  const yAt = f => y0 + (y1 - y0) * ease(f / Math.max(1, frames - 1));
  if (si < +first) {
    // fast-forward through this scene: its scroll in 0.5 s steps (poses and slides follow time, not frame counts)
    const n = Math.ceil(frames / 15);
    for (let k = 1; k <= n; k++) { await scrollTo(yAt(Math.min(frames - 1, k * 15))); await p.evaluate(ms => window.__step(ms), frames * STEP / n); }
    continue;
  }
  fs.mkdirSync(`${out}/${name}`, { recursive: true });
  const t0 = Date.now();
  for (let f = 0; f < frames; f++) {
    await scrollTo(yAt(f));
    await p.evaluate(ms => window.__step(ms), STEP);
    await p.screenshot({ path: `${out}/${name}/f${String(f).padStart(4, '0')}.jpg`, quality: 92, timeout: 180000 });
  }
  console.log(name, frames, 'frames', ((Date.now() - t0) / frames).toFixed(0), 'ms/frame');
}
await b.close();
