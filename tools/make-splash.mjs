// Banana's boot splash, in ROCKNIXDS Pixel's look (adapted from ROCKNIXDS's dii-ess-aye/rnds/gen_splash.mjs): the logo
// and "loading" on the top panel, the console waiting for a cartridge on the bottom one; dark and light, at 1280x480
// (RG DS) and 2048x768 (RG DS Plus). banana-ui shows it while it reads the library.
//   node tools/make-splash.mjs <rnds assets dir> <big logo png> <out dir>
import { chromium } from '/opt/node22/lib/node_modules/playwright/index.mjs';
import fs from 'fs';
const [rnds, logoPng, out] = process.argv.slice(2);
const b64 = f => fs.readFileSync(f).toString('base64');
const logo = `data:image/png;base64,${b64(logoPng)}`;
const shell = `data:image/png;base64,${b64(rnds + '/game_slot.png')}`;
const fontR = `data:font/ttf;base64,${b64(rnds + '/fonts/PixelifySans-Regular.ttf')}`;
const fontM = `data:font/ttf;base64,${b64(rnds + '/fonts/PixelifySans-Medium.ttf')}`;
const pal = {
  dark: { bg: '#121820', dot: 'rgba(255,255,255,.14)', dot2: 'rgba(255,255,255,.07)', ink: '#e7eef6', muted: '#93a0b0',
          edge: '#d5dee8', panel: '#121820', band: '#2e3a48', low: '#070a0e', mouth: '#05080b', brand: '#1b2330', shade: 'rgba(0,0,0,.45)' },
  light: { bg: '#e6ebf0', dot: 'rgba(20,35,58,.07)', dot2: 'rgba(20,35,58,.035)', ink: '#1d2733', muted: '#66758a',
           edge: '#2b3644', panel: '#edf1f6', band: '#ffffff', low: '#c5cfda', mouth: '#1d2733', brand: '#1b2330', shade: 'rgba(20,35,58,.25)' },
};
const html = p => `<html><head><style>
@font-face { font-family: PxR; src: url(${fontR}); } @font-face { font-family: PxM; src: url(${fontM}); }
* { margin: 0; padding: 0; box-sizing: border-box; }
body { width: 1280px; height: 480px; display: flex; background: ${p.bg}; image-rendering: pixelated; }
.scr { position: relative; width: 640px; height: 480px; overflow: hidden; background-color: ${p.bg};
  background-image: url("data:image/svg+xml,${encodeURIComponent(`<svg xmlns='http://www.w3.org/2000/svg' width='4' height='4' shape-rendering='crispEdges'><rect width='1' height='1' fill='${p.dot}'/><rect x='2' y='2' width='1' height='1' fill='${p.dot2}'/></svg>`)}");
  background-size: 4px 4px; }
.brand { position: absolute; left: 50%; top: 158px; transform: translateX(-50%); background: ${p.brand}; border-radius: 8px;
  padding: 14px 20px; box-shadow: 4px 4px 0 ${p.shade}; }
.brand img { height: 40px; display: block; }
.load { position: absolute; left: 0; right: 0; top: 270px; text-align: center; font: 20px PxM; letter-spacing: 2.4px; color: ${p.muted}; }
.dots { position: absolute; left: 50%; top: 306px; transform: translateX(-50%); display: flex; gap: 10px; }
.dots i { width: 10px; height: 10px; background: ${p.ink}; display: block; }
.dots i:nth-child(2) { opacity: .6; } .dots i:nth-child(3) { opacity: .3; }
.console { position: absolute; left: 150px; top: 300px; width: 340px; height: 200px; border: 3px solid ${p.edge}; border-radius: 18px;
  background: linear-gradient(${p.band} 0 8px, ${p.panel} 8px); box-shadow: inset 0 -6px 0 ${p.low}, 4px 4px 0 ${p.shade}; }
.mouth { position: absolute; left: 230px; top: 316px; width: 180px; height: 28px; border: 3px solid ${p.edge}; border-radius: 6px;
  background: ${p.mouth}; box-shadow: inset 0 6px 0 rgba(0,0,0,.5); }
.led { position: absolute; left: 446px; top: 324px; width: 14px; height: 10px; background: ${p.edge}; }
.led i { position: absolute; left: 2px; top: 2px; width: 10px; height: 6px; background: #3ce07a; }
.cart { position: absolute; left: 220px; top: 66px; width: 200px; height: 208px; overflow: hidden; }
.cart .art { position: absolute; left: 8px; top: 8px; width: 184px; height: 172px; background: linear-gradient(135deg, #b8860b, #f5d76e 55%, #e0a812); }
.cart .art b { position: absolute; left: 0; right: 0; top: 70px; text-align: center; font: 26px PxM; color: #fff; text-shadow: 3px 3px 0 rgba(0,0,0,.35); letter-spacing: 1px; }
.cart img { position: absolute; left: 0; top: 0; width: 200px; height: 208px; }
</style></head><body>
<div class="scr"><div class="brand"><img src="${logo}"></div><div class="load">LOADING</div><div class="dots"><i></i><i></i><i></i></div></div>
<div class="scr"><div class="console"></div><div class="cart"><div class="art"><b>BANANA</b></div><img src="${shell}"></div><div class="mouth"></div><div class="led"><i></i></div></div>
</body></html>`;
const b = await chromium.launch();
for (const [variant, p] of Object.entries(pal)) {
  for (const [scale, size] of [[1, 640], [1.6, 1024]]) {
    const page = await b.newPage({ viewport: { width: 1280, height: 480 }, deviceScaleFactor: scale });
    await page.setContent(html(p));
    await page.evaluate(() => document.fonts.ready);
    await page.waitForTimeout(100);
    await page.screenshot({ path: `${out}/${variant}-${size}.png` });
    await page.close();
  }
}
await b.close();
