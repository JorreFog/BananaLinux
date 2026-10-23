import { chromium } from 'playwright-core';
const [,, w, json] = process.argv;
const b = await chromium.launch({ executablePath: '/opt/pw-browsers/chromium-1194/chrome-linux/chrome', ignoreDefaultArgs: ['--headless=old'],
  args: ['--headless=new', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
const W = +w, H = Math.round(W * 9 / 16);
const p = await b.newPage({ viewport: { width: W, height: H } });
p.on('pageerror', e => console.log('error:', e.message));
await p.goto(`http://127.0.0.1:8765/scene.html?w=${W}`);
await p.waitForFunction(() => window.ready === true, null, { timeout: 120000 });
for (const s of JSON.parse(json)) { await p.evaluate(a => window.still(...a), s.slice(1)); await p.screenshot({ path: s[0] }); console.log('wrote', s[0]); }
await b.close();
