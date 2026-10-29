// The still layer of Banana's boot animation, in ROCKNIXDS Pixel's look (adapted from ROCKNIXDS's
// dii-ess-aye/rnds/gen_splash.mjs). banana-ui moves the rest (src/splash.cpp): the pixel bunch, the word, the
// loading dots. (One screen: there is no second panel, console or cartridge.)
//
//   node tools/make-splash.mjs <rnds assets dir> <pixel banana dir> <out dir>
//
// Writes, for each palette (dark, light) and asset size (640 and 1024: a panel wider than 640 takes the 1024 set and
// scales it, so a big panel reuses the sharper art):
//   <palette>-<size>-top.png   the panel background (the dotted screen)
// Positions are the ones src/splash.cpp uses (CSS px of a 640x480 panel, x1.6 on the Plus).
import { chromium } from '/opt/node22/lib/node_modules/playwright/index.mjs';
const [, , out] = process.argv.slice(2);
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
`;
const pages = {
  top: p => `<div class="scr"></div>`,
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
      await shot(scale, body(p, scale), css(p), `${out}/${variant}-${size}-${name}.png`);
}
await b.close();
