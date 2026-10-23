import * as THREE from "three";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";
import { MeshoptDecoder } from "three/addons/libs/meshopt_decoder.module.js";
import { EffectComposer } from "three/addons/postprocessing/EffectComposer.js";
import { RenderPass } from "three/addons/postprocessing/RenderPass.js";
import { UnrealBloomPass } from "three/addons/postprocessing/UnrealBloomPass.js";
import { OutputPass } from "three/addons/postprocessing/OutputPass.js";
import { ShaderPass } from "three/addons/postprocessing/ShaderPass.js";

const { gsap, ScrollTrigger } = window;
gsap.registerPlugin(ScrollTrigger);

const canvas = document.getElementById("stage");
const mobile = () => innerWidth < 900;

// ------------------------------------------------------------------ renderer and scene
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: true, powerPreference: "high-performance" });
renderer.setClearColor(0x000000, 0); // transparent: the page shows the menu's own background pattern through it
// Resolution: phones start lower, and it drops further if frames run long (see the loop).
const quality = { dpr: Math.min(devicePixelRatio, innerWidth < 900 ? 1.25 : 1.5), slow: 0, frames: 0 };
renderer.setPixelRatio(quality.dpr);
renderer.toneMapping = THREE.NeutralToneMapping;
renderer.toneMappingExposure = 1.0;

const scene = new THREE.Scene();
const pmrem = new THREE.PMREMGenerator(renderer);
// A product-photo studio as the environment: a dark room with softboxes, so the shell's edges and buttons
// pick up long, crisp highlights instead of an even grey sheen.
function studio() {
  const room = new THREE.Scene();
  room.add(new THREE.Mesh(new THREE.BoxGeometry(10, 10, 10),
    new THREE.MeshBasicMaterial({ color: 0x0b0b0e, side: THREE.BackSide })));
  const box = (w, h, x, y, z, k, warm = false) => {
    const m = new THREE.Mesh(new THREE.PlaneGeometry(w, h),
      new THREE.MeshBasicMaterial({ color: new THREE.Color(warm ? 1 : 0.92, warm ? 0.93 : 0.95, warm ? 0.84 : 1).multiplyScalar(k), side: THREE.DoubleSide }));
    m.position.set(x, y, z); m.lookAt(0, 0, 0); room.add(m);
  };
  box(6, 3, 0, 4.8, 1.0, 6);        // big overhead softbox
  box(1.2, 5, -4.8, 1.5, 1.5, 4);   // left strip
  box(1.2, 5, 4.8, 1.2, -1.0, 3, true); // right strip, warm
  box(5, 1.0, 0, 0.8, 4.8, 2.2);    // low front fill
  box(4, 1.2, 0, 2, -4.8, 3.5);     // back rim
  return room;
}
scene.environment = pmrem.fromScene(studio(), 0.02).texture;
scene.environmentIntensity = 0.85;

const camera = new THREE.PerspectiveCamera(30, 1, 0.02, 6);
const rig = new THREE.Group();     // scroll-driven pose
const spin = new THREE.Group();    // the pose's tilt
rig.add(spin);
scene.add(rig);

const key = new THREE.DirectionalLight(0xffffff, 1.1);
key.position.set(0.3, 0.6, 0.35);
scene.add(key);
const rim = new THREE.DirectionalLight(0xfff0d8, 1.6);
rim.position.set(-0.4, 0.25, -0.5);
scene.add(rim);

// A soft contact shadow drawn once, instead of a real-time shadow map.
const shadowCanvas = document.createElement("canvas");
shadowCanvas.width = shadowCanvas.height = 256;
{
  const c = shadowCanvas.getContext("2d");
  const g = c.createRadialGradient(128, 128, 10, 128, 128, 128);
  g.addColorStop(0, "rgba(0,0,0,0.9)"); g.addColorStop(0.45, "rgba(0,0,0,0.6)"); g.addColorStop(0.75, "rgba(0,0,0,0.2)"); g.addColorStop(1, "rgba(0,0,0,0)");
  c.fillStyle = g; c.fillRect(0, 0, 256, 256);
}
const ground = new THREE.Mesh(new THREE.PlaneGeometry(0.24, 0.17),
  new THREE.MeshBasicMaterial({ map: new THREE.CanvasTexture(shadowCanvas), transparent: true, depthWrite: false }));
ground.rotation.x = -Math.PI / 2;
ground.position.set(0, -0.0005, 0.005);
rig.add(ground);

// ------------------------------------------------------------------ post-processing
const composer = new EffectComposer(renderer);
composer.addPass(new RenderPass(scene, camera));
const bloom = new UnrealBloomPass(new THREE.Vector2(1, 1), 0.28, 0.32, 0.9);
composer.addPass(bloom);
// Bloom: desktop, dark theme only (on the light page it also broke the canvas's transparency), and not once the
// adaptive quality has given it up.
let bloomGivenUp = false;
const useBloom = () => { bloom.enabled = !bloomGivenUp && innerWidth >= 900 && document.documentElement.dataset.theme !== "light"; };
composer.addPass(new OutputPass());
// The finish: a touch of lens (chromatic aberration towards the corners), vignette and film grain.
const finish = new ShaderPass({
  uniforms: { tDiffuse: { value: null }, time: { value: 0 }, aspect: { value: 1 }, fringe: { value: 0.004 } },
  vertexShader: `varying vec2 vUv; void main(){ vUv = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }`,
  fragmentShader: `
    uniform sampler2D tDiffuse; uniform float time; uniform float aspect; uniform float fringe; varying vec2 vUv;
    float hash(vec2 p){ return fract(sin(dot(p, vec2(12.9898, 78.233)) + time * 13.7) * 43758.5453); }
    void main(){
      vec2 d = vUv - 0.5; d.x *= aspect;
      float r2 = dot(d, d);
      vec2 off = (vUv - 0.5) * r2 * fringe;
      vec4 base = texture2D(tDiffuse, vUv);
      vec3 col = vec3(texture2D(tDiffuse, vUv + off).r, base.g, texture2D(tDiffuse, vUv - off).b);
      col *= mix(1.0, 0.62, smoothstep(0.3, 1.3, r2));
      col += (hash(vUv * 900.0) - 0.5) * 0.03 * base.a;
      gl_FragColor = vec4(col, base.a);
    }`,
});
composer.addPass(finish);

// ------------------------------------------------------------------ the screens: BananaOS's own pictures
// The README's screenshots from the BananaLinux repo (docs/img/readme/shot-*.png on main, captured on the
// handheld): both 512x384 panels cut out of the drawn console around them, top over bottom in one 512x768
// image. They play as a slideshow, crossfading, while the handheld's buttons press as if someone were using it.
// The light theme: the README's one light capture, then banana-ui's own light frames, rendered headless from main
// (ui/banana-ui --headless) over a library with these games' names (no box art: the library slide is left out).
const SETS = {};
SETS.dark = [
  ["shot-home", "Home: your systems, recent games and play stats", "right"],
  ["shot-library", "Library: box art, play time and achievement progress", "a"],
  ["shot-options", "Per-game options: choose an emulator and settings for each game", "x"],
  ["shot-game", "In play: one DS screen on each display", "a"],
  ["shot-game-menu", "In-game menu: save, load and adjust quick settings", "b"],
  ["shot-store", "App Store: install and update apps from the menu", "start"],
  ["shot-settings", "Settings: six groups, with help on the top screen", "start"],
  ["shot-carts", "Carts: browse your games on a 3D shelf. Experimental", "right"],
  ["shot-zh-home", "Simplified Chinese: translation by Majirefy", "right"],
];
SETS.light = [
  ["shot-light", "Light theme: the same interface in a lighter palette", "right"],
  ["light-options", "Per-game options: choose an emulator and settings for each game", "x"],
  ["light-settings", "Settings: six groups, with help on the top screen", "start"],
];
let theme = document.documentElement.dataset.theme === "light" ? "light" : "dark";
let SLIDES = SETS[theme];
const SLIDE_TIME = 3.6, FADE = 0.3;
const screenTop = document.createElement("canvas"), screenBottom = document.createElement("canvas");
for (const c of [screenTop, screenBottom]) { c.width = 512; c.height = 384; }
const texTop = new THREE.CanvasTexture(screenTop);
const texBottom = new THREE.CanvasTexture(screenBottom);
for (const t of [texTop, texBottom]) {
  t.colorSpace = THREE.SRGBColorSpace;
  t.flipY = false;
  t.anisotropy = 8;
  t.generateMipmaps = false;
  t.minFilter = THREE.LinearFilter;
}
const images = {};   // by name, once loaded
const loadImage = (src) => new Promise((ok) => { const i = new Image(); i.onload = () => ok(i); i.onerror = () => ok(null); i.src = src; });
const loadSlide = (name) => images[name] ? Promise.resolve(images[name])
  : loadImage(`assets/screens/${name}.webp`).then((img) => (images[name] = img));
const loadSet = (set) => set.forEach(([name]) => loadSlide(name));
const slides = { on: false, i: 0, t: 0, power: 0, cur: null, prev: null };
const caption = document.getElementById("caption");
let ledGreen = null;

// ------------------------------------------------------------------ dark / light
// Light: a white handheld (warm white shell, light grey buttons, dark print) and the light menu on its screens.
const SHELL_LIGHT = {
  Shell: "#e4e1da", ShellRecess: "#c9c5bc", Button: "#e4e1da", ButtonDark: "#d6d3cb", DPad: "#cbc8bf",
  Rubber: "#a8a59d", StickCap: "#e4e2db", Label: "#3a3732", Print: "#6b665c",
};
const shellMats = {};
const screenMats = [];
function paintDevice() {
  for (const [name, { m, dark }] of Object.entries(shellMats)) {
    if (theme === "light") m.color.set(SHELL_LIGHT[name]).convertSRGBToLinear(); else m.color.copy(dark);
  }
  // Light: the colour fringe goes (on a light page it showed as a blue halo round the white shell), and the
  // lighting turns directional (a strong key from the upper left, less of the all-round studio) so a white
  // shell gets light and shade on its faces instead of reading flat.
  const L = theme === "light";
  finish.uniforms.fringe.value = L ? 0 : 0.004;
  scene.environmentIntensity = L ? 0.38 : 0.85;
  key.intensity = L ? 2.6 : 1.1;
  key.position.set(L ? -0.35 : 0.3, 0.6, L ? 0.25 : 0.35);
  rim.intensity = L ? 0.9 : 1.6;
  for (const name of ["Shell", "Button"]) if (shellMats[name]) shellMats[name].m.roughness = L ? 0.42 : 0.52;
  renderer.toneMappingExposure = L ? 1.08 : 1.0;
  // backlit panels: brighter than 1 in the dark (the whites catch the bloom), plain on the light page
  const k = theme === "light" ? 0.92 : 1.25;
  for (const m of screenMats) m.color.setScalar(k);
  useBloom();
}
const themeBtn = document.getElementById("theme");
function setTheme(t, fromClick) {
  theme = t;
  document.documentElement.dataset.theme = t === "light" ? "light" : "";
  if (t !== "light") delete document.documentElement.dataset.theme;
  themeBtn.setAttribute("aria-checked", t === "light");
  document.getElementById("theme-label").textContent = t === "light" ? "Light" : "Dark";
  themeBtn.setAttribute("aria-label", t === "light" ? "Switch to dark theme" : "Switch to light theme");
  try { localStorage.setItem("theme", t); } catch (e) { /* private mode: just this visit */ }
  paintDevice();
  if (!fromClick) return;
  SLIDES = SETS[t];
  loadSet(SLIDES);
  loadSlide(SLIDES[0][0]).then(() => { slides.started = true; show(0); });
}
themeBtn.addEventListener("click", () => setTheme(theme === "light" ? "dark" : "light", true));

let screensDirty = true;
function drawScreens() {
  if (!screensDirty) return;
  const k = Math.min(1, slides.t / FADE);
  screensDirty = slides.on && (slides.power < 1 || (slides.started && k < 1));
  const cur = slides.cur, prev = slides.prev;
  for (const [c, sy] of [[screenTop, 0], [screenBottom, 384]]) {
    const g = c.getContext("2d");
    g.globalAlpha = 1;
    g.fillStyle = "#000"; g.fillRect(0, 0, 512, 384);
    if (!slides.on) continue;
    g.globalAlpha = slides.power;
    if (prev && k < 1 && slides.started) g.drawImage(prev, 0, sy, 512, 384, 0, 0, 512, 384);
    g.globalAlpha = slides.power * (slides.started ? k : 1);
    if (cur) g.drawImage(cur, 0, sy, 512, 384, 0, 0, 512, 384);
  }
  texTop.needsUpdate = texBottom.needsUpdate = true;
}

function stepSlides(dt) {
  if (!slides.on) return;
  slides.power = Math.min(1, slides.power + dt * 2);
  slides.t += dt;
  if (slides.t < SLIDE_TIME) return;
  screensDirty = true;
  slides.t = 0;
  slides.started = true;
  do slides.i = (slides.i + 1) % SLIDES.length; while (!images[SLIDES[slides.i][0]] && slides.i !== 0);
  show(slides.i);
}

// crossfade from whatever is on the screens to slide i of the current set
function show(i) {
  slides.i = i;
  slides.prev = slides.cur;
  slides.cur = images[SLIDES[i][0]] || slides.cur;
  slides.t = 0;
  screensDirty = true;
  pressFor(SLIDES[i][2]);
  caption.textContent = SLIDES[i][1];
}

// ------------------------------------------------------------------ load
const loadbar = document.getElementById("loadbar");
const [gltf] = await Promise.all([
  new GLTFLoader().setMeshoptDecoder(MeshoptDecoder).loadAsync("rgds_plus.min.glb", (e) => { if (e.total) loadbar.style.width = `${Math.min(1, e.loaded / e.total) * 90}%`; }),
  loadSlide(SLIDES[0][0]).then((img) => { slides.cur = img; }),
]);
loadSet(SLIDES);   // the rest load while the first one shows
loadbar.style.width = "100%";
drawScreens();
setTimeout(() => loadSet(SETS[theme === "light" ? "dark" : "light"]), 4000);   // the other set, for a quick switch

const model = gltf.scene;
spin.add(model);
let hinge = null;
const pressables = [];
model.traverse((o) => {
  if (o.isMesh) {
    const mats = Array.isArray(o.material) ? o.material : [o.material];
    mats.forEach((m, i) => {
      let rep = null;
      if (m.name === "Glass") rep = new THREE.MeshBasicMaterial({ color: 0x050507 });
      // backlit panels: a little over 1 so the menu's whites catch the bloom
      if (m.name === "ScreenTop") screenMats.push(rep = new THREE.MeshBasicMaterial({ map: texTop }));
      if (m.name === "ScreenBottom") screenMats.push(rep = new THREE.MeshBasicMaterial({ map: texBottom }));
      if (m.name === "LedGreen") ledGreen = m;
      if (SHELL_LIGHT[m.name] && !shellMats[m.name]) shellMats[m.name] = { m, dark: m.color.clone() };
      if (m.name === "Shell" || m.name === "Button") { m.roughness = 0.52; m.metalness = 0.0; }
      if (m.name === "ButtonDark") { m.roughness = 0.3; }
      if (m.name === "DPad" || m.name === "StickCap") { m.roughness = 0.42; }
      if (rep) { rep.name = m.name; if (Array.isArray(o.material)) o.material[i] = rep; else o.material = rep; }
    });
  }
});
hinge = model.getObjectByName("LidHinge");
setTheme(theme, false);
model.traverse((o) => {
  if (o.name.startsWith("Btn_") && !o.parent.name.startsWith("Btn_")) {
    o.userData.rest = o.position.clone();
    const box = new THREE.Box3();
    o.traverse((m) => { if (m.isMesh) { m.geometry.computeBoundingBox(); box.union(m.geometry.boundingBox); } });
    o.userData.center = box.getCenter(new THREE.Vector3());
    pressables.push(o);
  }
});

// ------------------------------------------------------------------ pose: intro and scroll
// Each section has a pose: where the device sits, how it's turned, how far the lid is open, the camera distance.
const POSES = {
  intro:    { x: 0.0, y: 0.0, yaw: -2.6, pitch: 0.0, lid: 0, dist: 0.75 },
  hero:     { x: 0.045, y: 0.0, yaw: -0.32, pitch: 0.0, lid: 150, dist: 0.43 },
  features: { x: 0.055, y: 0.0, yaw: -0.85, pitch: 0.05, lid: 115, dist: 0.42 },
  emulators: { x: 0.0, y: 0.075, yaw: 0.35, pitch: 0.0, lid: 150, dist: 0.85 },
  inside:   { x: -0.06, y: 0.0, yaw: 0.75, pitch: 0.1, lid: 150, dist: 0.38 },
  get:      { x: 0.0, y: 0.055, yaw: 6.28 - 0.25, pitch: 0.0, lid: 150, dist: 0.62 },
};
const pose = { ...POSES.intro };

function applyPose(t) {
  const m = mobile();
  rig.position.set(m ? 0 : pose.x, (m ? 0 : pose.y) + Math.sin(t * 1.1) * 0.0025, 0);
  rig.rotation.y = pose.yaw;
  spin.rotation.set(pose.pitch, 0, 0);
  if (hinge) hinge.rotation.set(THREE.MathUtils.degToRad(180 - pose.lid), 0, 0);
  // on a narrow screen, back off far enough that the handheld (about 0.2 m across) fits the width
  const hfov = 2 * Math.atan(Math.tan(THREE.MathUtils.degToRad(camera.fov / 2)) * camera.aspect);
  const d = Math.max(pose.dist, m ? 0.118 / Math.tan(hfov / 2) : 0);
  camera.position.set(0, d * 0.62, d * 0.8);
  camera.lookAt(0, 0.032, 0);
}

function sectionsTimeline() {
  const tl = gsap.timeline({
    scrollTrigger: { trigger: "main", start: "top top", end: "bottom bottom", scrub: 1.2 },
    defaults: { ease: "power2.inOut", duration: 1 },
  });
  tl.to(pose, POSES.features).to(pose, POSES.emulators).to(pose, POSES.inside).to(pose, POSES.get);
  return tl;
}

// the button press that goes with each new screen
const KEY_MESH = { a: "Btn_A", b: "Btn_B", x: "Btn_X", start: "Btn_Start", right: "Btn_DPad", down: "Btn_DPad" };
function pressFor(btn) {
  const mesh = pressables.find((p) => p.name === KEY_MESH[btn]);
  if (mesh) mesh.userData.pressedAt = performance.now();
}

// button press animation: face buttons go down, shoulders forward, side buttons in
function animateButtons(now) {
  for (const b of pressables) {
    const t = b.userData.pressedAt ? (now - b.userData.pressedAt) / 170 : 1;
    const depth = t < 1 ? Math.sin(Math.PI * t) * 0.6 : 0;
    const p = b.userData.rest;
    b.position.copy(p);
    if (/^Btn_[LR][12]$/.test(b.name)) b.position.z += depth;
    else if (b.name === "Btn_Volume") b.position.x += depth;
    else if (b.name === "Btn_Power" || b.name === "Btn_SideR") b.position.x -= depth;
    else b.position.y -= depth;
  }
}

// ------------------------------------------------------------------ page bits
function pageAnimations() {
  // phones: the caption sits over the cards' text once they scroll up, so it only shows on the first screen
  gsap.utils.toArray(".reveal").forEach((el) => {
    gsap.to(el, { opacity: 1, y: 0, duration: 0.9, ease: "power3.out",
      scrollTrigger: { trigger: el, start: "top 85%", toggleActions: "play none none reverse" } });
    el.querySelectorAll(".tile, .facts li, .emu").forEach((child, i) => {
      gsap.from(child, { opacity: 0, x: -24, duration: 0.5, delay: 0.15 + i * 0.06, ease: "power2.out",
        scrollTrigger: { trigger: el, start: "top 80%", toggleActions: "play none none reverse" } });
    });
  });
  document.querySelectorAll("[data-count]").forEach((el) => {
    const n = { v: 0 };
    gsap.to(n, { v: +el.dataset.count, duration: 1.4, ease: "power2.out", snap: { v: 1 },
      onUpdate: () => { el.textContent = n.v; },
      scrollTrigger: { trigger: el, start: "top 85%" } });
  });
}

// ------------------------------------------------------------------ intro
function intro() {
  document.getElementById("loader").classList.add("done");
  const tl = gsap.timeline();
  tl.to(pose, { ...POSES.hero, lid: 0, duration: 2.0, ease: "power3.out" }, 0.2)
    .to(pose, { lid: POSES.hero.lid, duration: 1.3, ease: "back.out(1.4)" }, 1.4)
    .add(() => { slides.on = true; screensDirty = true; caption.textContent = SLIDES[0][1]; }, 1.9)
    .from("#hero .card", { opacity: 0, y: 50, duration: 1, ease: "power3.out" }, 1.6)
    .from("nav.top", { opacity: 0, y: -20, duration: 0.8 }, 1.8)
    .add(() => {
      document.querySelector("#hero .card").classList.remove("reveal");
      document.querySelector("#hero .card").style.opacity = 1;
      document.querySelector("#hero .card").style.transform = "none";
      sectionsTimeline();
      pageAnimations();
    }, 2.6);
}
// no reveal-from-hidden on the hero card: the intro brings it in
document.querySelector("#hero .card").style.opacity = 1;
document.querySelector("#hero .card").style.transform = "none";

// ------------------------------------------------------------------ loop
function resize() {
  const w = innerWidth, h = innerHeight;
  renderer.setSize(w, h, false);
  composer.setPixelRatio(renderer.getPixelRatio());
  composer.setSize(w, h);
  useBloom();
  camera.aspect = w / h;
  // phones: the cards fill the bottom half, so shift the picture up into the top half
  if (mobile()) camera.setViewOffset(w, h, 0, h * 0.24, w, h); else camera.clearViewOffset();
  camera.updateProjectionMatrix();
  finish.uniforms.aspect.value = w / h;
}
addEventListener("resize", resize);
resize();

const clock = new THREE.Clock();
renderer.setAnimationLoop(() => {
  const raw = clock.getDelta();
  const dt = Math.min(raw, 0.05);
  // If frames keep running long (under ~40 fps), step the resolution down; then the bloom goes.
  if (raw < 0.25) { quality.frames++; if (raw > 0.025) quality.slow++; }
  if (quality.frames >= 90) {
    if (quality.slow > 45) {
      if (quality.dpr > 0.8) { quality.dpr = Math.max(0.75, quality.dpr - 0.25); renderer.setPixelRatio(quality.dpr); resize(); }
      else { bloomGivenUp = true; useBloom(); }
    }
    quality.frames = quality.slow = 0;
  }
  const t = clock.elapsedTime;
  applyPose(t);
  stepSlides(dt);
  caption.style.opacity = scrollY > innerHeight * 0.3 ? 0 : 1;   // the caption belongs to the first screen
  drawScreens();
  animateButtons(performance.now());
  if (ledGreen) ledGreen.emissiveIntensity = 2.5 + Math.sin(t * 2) * 0.8;
  finish.uniforms.time.value = t;
  composer.render();
});

intro();
