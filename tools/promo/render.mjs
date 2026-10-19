// renders scene.html frame by frame in headless Chromium (SwiftShader WebGL) to PNGs
import { chromium } from 'playwright-core';
import fs from 'fs';
const [,, w = '1920', from = '0', to = '', step = '1', out = 'frames', color = ''] = process.argv;
fs.mkdirSync(out, { recursive: true });
const b = await chromium.launch({ executablePath: '/opt/pw-browsers/chromium-1194/chrome-linux/chrome',
  ignoreDefaultArgs: ['--headless=old'], args: ['--headless=new', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist', '--enable-webgl'] });
const W = +w, H = Math.round(W * 9 / 16);
const p = await b.newPage({ viewport: { width: W, height: H } });
p.on('console', m => console.log('page:', m.text()));
p.on('pageerror', e => console.log('error:', e.message));
await p.goto(`http://127.0.0.1:8765/scene.html?w=${W}${color ? '&color=' + encodeURIComponent(color) : ''}`);
await p.waitForFunction(() => window.ready === true, null, { timeout: 120000 });
const dur = await p.evaluate(() => window.DURATION);
const last = to === '' ? Math.ceil(dur * 30) : +to;
for (let f = +from; f < last; f += +step) {
  const t0 = Date.now();
  await p.evaluate(t => window.renderAt(t), f / 30);
  await p.screenshot({ path: `${out}/f${String(f).padStart(5, '0')}.png` });
  if (f % 30 === 0) console.log('frame', f, Date.now() - t0, 'ms');
}
await b.close();
