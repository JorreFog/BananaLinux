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
const banana = `data:image/png;base64,${b64(bananas + '/banana-48.png')}`;
const pal = {
  dark: { bg: '#121820', dot: 'rgba(255,255,255,.14)', dot2: 'rgba(255,255,255,.07)', edge: '#d5dee8', panel: '#121820',
          band: '#2e3a48', low: '#070a0e', mouth: '#05080b', shade: 'rgba(0,0,0,.45)' },
  light: { bg: '#e6ebf0', dot: 'rgba(20,35,58,.07)', dot2: 'rgba(20,35,58,.035)', edge: '#2b3644', panel: '#edf1f6',
           band: '#ffffff', low: '#c5cfda', mouth: '#1d2733', shade: 'rgba(20,35,58,.25)' },
};
const css = p => `
* { margin: 0; padding: 0; box-sizing: border-box; }
body { width: 640px; height: 480px; background: transparent; image-rendering: pixelated; }
.scr { position: absolute; left: 0; top: 0; width: 640px; height: 480px; overflow: hidden; background-color: ${p.bg};
  background-image: url("data:image/svg+xml,${encodeURIComponent(`<svg xmlns='http://www.w3.org/2000/svg' width='4' height='4' shape-rendering='crispEdges'><rect width='1' height='1' fill='${p.dot}'/><rect x='2' y='2' width='1' height='1' fill='${p.dot2}'/></svg>`)}");
  background-size: 4px 4px; }
.console { position: absolute; left: 150px; top: 300px; width: 340px; height: 200px; border: 3px solid ${p.edge}; border-radius: 18px;
  background: linear-gradient(${p.band} 0 8px, ${p.panel} 8px); box-shadow: inset 0 -6px 0 ${p.low}, 4px 4px 0 ${p.shade}; }
.mouth { position: absolute; left: 230px; top: 316px; width: 180px; height: 28px; border: 3px solid ${p.edge}; border-radius: 6px;
  background: ${p.mouth}; box-shadow: inset 0 6px 0 rgba(0,0,0,.5); }
.led { position: absolute; left: 446px; top: 324px; width: 14px; height: 10px; background: ${p.edge}; }
.led i { position: absolute; left: 2px; top: 2px; width: 10px; height: 6px; background: #7a2a30; }
.lip { position: absolute; left: 230px; top: 325px; width: 180px; height: 19px; border: 3px solid ${p.edge}; border-top: 0;
  border-radius: 0 0 6px 6px; background: ${p.mouth}; }
.lip::before { content: ''; position: absolute; left: -3px; right: -3px; top: 0; height: 3px; background: ${p.edge}; }
.cart { position: absolute; left: 0; top: 0; width: 200px; height: 208px; }
.cart .art { position: absolute; left: 8px; top: 8px; width: 184px; height: 172px;
  background: linear-gradient(135deg, #07091f, #26307a 58%, #4a2c86); }
.cart .art img { position: absolute; left: 44px; top: 38px; width: 96px; height: 96px; }
.cart > img { position: absolute; left: 0; top: 0; width: 200px; height: 208px; }
`;
const pages = {
  top: p => `<div class="scr"></div>`,
  bot: p => `<div class="scr"><div class="console"></div><div class="mouth"></div><div class="led"><i></i></div></div>`,
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
      await shot(scale, body(p), css(p), `${out}/${variant}-${size}-${name}.png`,
        name === 'lip' ? { x: 227, y: 325, width: 186, height: 22 } : undefined);
  await shot(scale, `<div class="cart"><div class="art"><img src="${banana}"></div><img src="${shell}"></div>`, css(pal.dark),
    `${out}/cart-${size}.png`, { x: 0, y: 0, width: 200, height: 208 });
}
await b.close();
