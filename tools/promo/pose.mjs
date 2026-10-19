import { chromium } from 'playwright-core';
const b = await chromium.launch({ executablePath: '/opt/pw-browsers/chromium-1194/chrome-linux/chrome', ignoreDefaultArgs: ['--headless=old'],
  args: ['--headless=new', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
const p = await b.newPage({ viewport: { width: 960, height: 540 } });
p.on('pageerror', e => console.log('error:', e.message));
await p.goto('http://127.0.0.1:8765/scene.html?w=960');
await p.waitForFunction(() => window.ready === true);
const poses = JSON.parse(process.argv[2]);
for (const [name, deg, pos, at, fov] of poses) { await p.evaluate(a => window.pose(...a), [deg, pos, at, fov]); await p.screenshot({ path: `key/${name}.png` }); }
await b.close();
