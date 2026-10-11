// The still layers of Banana's boot animation, in ROCKNIXDS Pixel's look (adapted from ROCKNIXDS's
// dii-ess-aye/rnds/gen_splash.mjs). banana-ui moves the rest (src/splash.cpp): the pixel bunch, the word, the
// loading dots, the cartridge going into the console.
//
//   node tools/make-splash.mjs <rnds assets dir> <pixel banana dir> <out dir>
//
// Writes, for each palette (dark, light) and panel size (640 = RG DS, 1024 = RG DS Plus):
//   <palette>-<size>-top.png   the top panel's background (the dotted screen)
//   <palette>-<size>-bot.png   the bottom panel: the console with its slot, the power light off
//   <palette>-<size>-lip.png   the slot's front lip, drawn over the cartridge as it goes in
// and for each size: cart-<size>.png, the cartridge (transparent around it) with the bunch on a label in the navy
// and violet of the BananaUI / Helios marks.
// Positions are the ones src/splash.cpp uses (CSS px of a 640x480 panel, x1.6 on the Plus).
import { chromium } from '/opt/node22/lib/node_modules/playwright/index.mjs';
import fs from 'fs';
const [rnds, bananas, out] = process.argv.slice(2);
const b64 = f => fs.readFileSync(f).toString('base64');
const shell = `data:image/png;base64,${b64(rnds + '/game_slot.png')}`;
// the console: the back of a DSi XL as pixel art (tools/make-console.py), per palette and scale, its top at 322
const consoleOf = (name, scale) => `data:image/png;base64,${b64(rnds + '/console-' + name + (scale > 1 ? '@1.6x' : '@1x') + '.png')}`;
const banana = `data:image/png;base64,${b64(bananas + '/banana-48.png')}`;
const pal = {
  dark: { name: 'dark', bg: '#17161c', dot: 'rgba(255,255,255,.14)', dot2: 'rgba(255,255,255,.07)', edge: '#e8e2d3', panel: '#17161c',
          band: '#3a3540', low: '#0b0a0d', mouth: '#070609', shade: 'rgba(0,0,0,.45)' },
  light: { name: 'light', bg: '#f3efe6', dot: 'rgba(20,35,58,.07)', dot2: 'rgba(20,35,58,.035)', edge: '#3a342c', panel: '#f6f2ea',
           band: '#ffffff', low: '#d8d0c0', mouth: '#2a2620', shade: 'rgba(20,35,58,.25)' },
};
const css = p => `
* { margin: 0; padding: 0; box-sizing: border-box; }
body { width: 640px; height: 480px; background: transparent; image-rendering: pixelated; }
.scr { position: absolute; left: 0; top: 0; width: 640px; height: 480px; overflow: hidden; background-color: ${p.bg};
  background-image: url("data:image/svg+xml,${encodeURIComponent(`<svg xmlns='http://www.w3.org/2000/svg' width='4' height='4' shape-rendering='crispEdges'><rect width='1' height='1' fill='${p.dot}'/><rect x='2' y='2' width='1' height='1' fill='${p.dot2}'/></svg>`)}");
  background-size: 4px 4px; }
.console { position: absolute; left: 40px; top: 322px; width: 560px; height: 172px; }
.mouth { position: absolute; left: 260px; top: 316px; width: 120px; height: 28px; border: 3px solid ${p.edge}; border-radius: 6px;
  background: ${p.mouth}; box-shadow: inset 0 6px 0 rgba(0,0,0,.5); }
.led { position: absolute; left: 409px; top: 336px; width: 14px; height: 10px; background: ${p.edge}; }
.led i { position: absolute; left: 2px; top: 2px; width: 10px; height: 6px; background: #7a2a30; }
.lip { position: absolute; left: 260px; top: 325px; width: 120px; height: 19px; border: 3px solid ${p.edge}; border-top: 0;
  border-radius: 0 0 6px 6px; background: ${p.mouth}; }
.lip::before { content: ''; position: absolute; left: -3px; right: -3px; top: 0; height: 3px; background: ${p.edge}; }
.cart { position: absolute; left: 0; top: 0; width: 100px; height: 104px; }
.cart .art { position: absolute; left: 4px; top: 4px; width: 92px; height: 86px;
  background: linear-gradient(135deg, #1f1405, #7a5a10 58%, #b8861e); }
.cart .art img { position: absolute; left: 22px; top: 19px; width: 48px; height: 48px; }
.cart > img { position: absolute; left: 0; top: 0; width: 100px; height: 104px; }
`;
const pages = {
  top: p => `<div class="scr"></div>`,
  bot: (p, scale) => `<div class="scr"><img class="console" src="${consoleOf(p.name, scale)}"><div class="mouth"></div><div class="led"><i></i></div></div>`,
  lip: p => `<div class="lip"></div>`,
};
const b = await chromium.launch();
const shot = async (scale, body, style, path, clip) => {
  const page = await b.newPage({ viewport: { width: 640, height: 480 }, deviceScaleFactor: scale });
  await page.setContent(`<html><head><style>${style}</style></head><body>${body}</body></html>`);
  await page.waitForTimeout(50);
  await page.screenshot({ path, omitBackground: true, clip });
  await page.close();
};
for (const [scale, size] of [[1, 640], [1.6, 1024]]) {
  for (const [variant, p] of Object.entries(pal))
    for (const [name, body] of Object.entries(pages))
      await shot(scale, body(p, scale), css(p), `${out}/${variant}-${size}-${name}.png`,
        name === 'lip' ? { x: 257, y: 325, width: 126, height: 22 } : undefined);
  await shot(scale, `<div class="cart"><div class="art"><img src="${banana}"></div><img src="${shell}"></div>`, css(pal.dark),
    `${out}/cart-${size}.png`, { x: 0, y: 0, width: 100, height: 104 });
}
await b.close();
