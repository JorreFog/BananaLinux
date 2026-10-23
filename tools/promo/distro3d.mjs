// renders distro3d.html's poses: per pose a picture of the handheld (PNG, transparent around it), its screens' mask
// (PNG: top red, bottom green) and where the screens' corners land (JSON), for distro.py
//   node distro3d.mjs <port> <width> <height> <jobs.json>      jobs: [[outPrefix, pose], ...]
import { chromium } from 'playwright-core';
import fs from 'fs';
const [,, port, w, h, jobsFile] = process.argv;
const jobs = JSON.parse(fs.readFileSync(jobsFile, 'utf8'));
const b = await chromium.launch({ executablePath: process.env.CHROME || '/opt/pw-browsers/chromium-1194/chrome-linux/chrome', ignoreDefaultArgs: ['--headless=old'],
  args: ['--headless=new', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
const p = await b.newPage({ viewport: { width: +w, height: +h } });
p.on('pageerror', e => console.log('error:', e.message));
await p.goto(`http://127.0.0.1:${port}/distro3d.html?w=${w}&h=${h}`);
await p.waitForFunction(() => window.ready === true, null, { timeout: 180000 });
const t0 = Date.now();
for (const [out, pose] of jobs) {
  if (fs.existsSync(out + '.json')) continue;		// done in an earlier run
  const c = await p.evaluate(a => window.shot(a), pose);
  await p.screenshot({ path: out + '.png', omitBackground: true });
  await p.evaluate(() => window.mask(true));
  await p.screenshot({ path: out + '-mask.png' });
  await p.evaluate(() => window.mask(false));
  fs.writeFileSync(out + '.json', JSON.stringify(c));
}
console.log(jobs.length, 'poses', ((Date.now() - t0) / Math.max(1, jobs.length)).toFixed(0), 'ms each');
await b.close();
