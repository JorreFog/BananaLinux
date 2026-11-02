// The BananaOS showcase: the RG DS Plus model from the project's own site (web/preview/rgds_plus.min.glb), its lid
// opening when the window comes into view, BananaOS's screenshots playing on both screens. Drag to turn it.
import * as THREE from "three";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";
import { MeshoptDecoder } from "three/addons/libs/meshopt_decoder.module.js";

const SCREENS = "../preview/assets/screens/";
const SETS = {
  ui: [
    ["shot-home", "Hem: system, senaste spel och speltid"],
    ["shot-library", "Bibliotek: omslag, speltid och achievements"],
    ["shot-options", "Per spel: välj emulator och inställningar"],
    ["shot-game-menu", "Spelmeny: spara, ladda och snabbinställningar"],
    ["shot-store", "App Store: installera och uppdatera appar"],
    ["shot-settings", "Inställningar, med hjälp på övre skärmen"],
    ["shot-carts", "Carts: spelen på en 3D-hylla"],
    ["shot-zh-home", "Förenklad kinesiska, översatt av Majirefy"],
  ],
  themes: [
    "comic-panel", "coral-reef", "felt-table", "neon-arcade", "oak-bookshelf",
    "orbital-hud", "paper-notebook", "phosphor-terminal", "swiss-poster", "vinyl-lounge",
  ].map((t) => [`theme-${t}`, "Tema: " + t.split("-").map((w) => w[0].toUpperCase() + w.slice(1)).join(" ").replace("Hud", "HUD")]),
};
const SLIDE_TIME = 3.4, FADE = 0.35;
const reduced = matchMedia("(prefers-reduced-motion: reduce)").matches;

export async function mount(host) {
  const canvas = host.querySelector("#stage");
  const loadEl = host.querySelector("#device-load");
  const bar = host.querySelector("#device-bar");
  const caption = host.querySelector("#device-caption");
  const small = () => host.clientWidth < 560;

  const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: true, powerPreference: "high-performance" });
  renderer.setClearColor(0x000000, 0);
  renderer.setPixelRatio(Math.min(devicePixelRatio, small() ? 1.75 : 1.5));
  renderer.toneMapping = THREE.NeutralToneMapping;

  const scene = new THREE.Scene();
  // a dark studio with softboxes as the environment, so the shell's edges catch long highlights
  const pmrem = new THREE.PMREMGenerator(renderer);
  const room = new THREE.Scene();
  room.add(new THREE.Mesh(new THREE.BoxGeometry(10, 10, 10), new THREE.MeshBasicMaterial({ color: 0x0b0b0e, side: THREE.BackSide })));
  const box = (w, h, x, y, z, k, c = [0.92, 0.95, 1]) => {
    const m = new THREE.Mesh(new THREE.PlaneGeometry(w, h), new THREE.MeshBasicMaterial({ color: new THREE.Color(...c).multiplyScalar(k), side: THREE.DoubleSide }));
    m.position.set(x, y, z); m.lookAt(0, 0, 0); room.add(m);
  };
  box(6, 3, 0, 4.8, 1, 6);
  box(1.2, 5, -4.8, 1.5, 1.5, 4);                      // left strip
  box(1.2, 5, 4.8, 1.2, -1, 3.2, [1, 0.93, 0.84]);     // right strip, warm
  box(5, 1, 0, 0.8, 4.8, 2.2);
  box(4, 1.2, 0, 2, -4.8, 3.5);
  scene.environment = pmrem.fromScene(room, 0.02).texture;
  scene.environmentIntensity = 0.85;
  const key = new THREE.DirectionalLight(0xffffff, 1.1); key.position.set(0.3, 0.6, 0.35); scene.add(key);
  const rim = new THREE.DirectionalLight(0xfff0d8, 1.6); rim.position.set(-0.4, 0.25, -0.5); scene.add(rim);

  const camera = new THREE.PerspectiveCamera(30, 1, 0.02, 6);
  const rig = new THREE.Group(), tilt = new THREE.Group();
  rig.add(tilt); scene.add(rig);

  // ---------------------------------------------------------------- the two screens
  const mk = () => { const c = document.createElement("canvas"); c.width = 512; c.height = 384; return c; };
  const cTop = mk(), cBot = mk();
  const tTop = new THREE.CanvasTexture(cTop), tBot = new THREE.CanvasTexture(cBot);
  for (const t of [tTop, tBot]) { t.colorSpace = THREE.SRGBColorSpace; t.flipY = false; t.anisotropy = 8; t.generateMipmaps = false; t.minFilter = THREE.LinearFilter; }
  const images = {};
  const load = (name) => images[name] ? Promise.resolve(images[name]) : new Promise((ok) => {
    const i = new Image(); i.onload = () => ok(images[name] = i); i.onerror = () => ok(null); i.src = SCREENS + name + ".webp";
  });
  const S = { set: "ui", i: 0, t: 0, power: 0, on: false, cur: null, prev: null, dirty: true };
  function draw() {
    if (!S.dirty) return;
    const k = Math.min(1, S.t / FADE);
    S.dirty = S.on && (S.power < 1 || k < 1);
    for (const [c, sy] of [[cTop, 0], [cBot, 384]]) {
      const g = c.getContext("2d");
      g.globalAlpha = 1; g.fillStyle = "#000"; g.fillRect(0, 0, 512, 384);
      if (!S.on) continue;
      if (S.prev && k < 1) { g.globalAlpha = S.power; g.drawImage(S.prev, 0, sy, 512, 384, 0, 0, 512, 384); }
      if (S.cur) { g.globalAlpha = S.power * (S.prev ? k : 1); g.drawImage(S.cur, 0, sy, 512, 384, 0, 0, 512, 384); }
    }
    tTop.needsUpdate = tBot.needsUpdate = true;
  }
  async function show(i) {
    const list = SETS[S.set];
    S.i = (i + list.length) % list.length;
    const img = await load(list[S.i][0]);
    S.prev = S.cur; S.cur = img || S.cur; S.t = 0; S.dirty = true;
    caption.textContent = list[S.i][1];
    load(list[(S.i + 1) % list.length][0]);   // the next one, ahead of time
  }
  host.querySelectorAll(".chips button").forEach((b) => b.addEventListener("click", () => {
    host.querySelectorAll(".chips button").forEach((x) => x.setAttribute("aria-pressed", String(x === b)));
    S.set = b.dataset.set; S.on = true; S.power = Math.max(S.power, 0.6);
    show(0);
  }));

  // ---------------------------------------------------------------- the model
  const [gltf] = await Promise.all([
    new GLTFLoader().setMeshoptDecoder(MeshoptDecoder).loadAsync("../preview/rgds_plus.min.glb", (e) => {
      if (e.total) bar.style.width = `${Math.min(1, e.loaded / e.total) * 100}%`;
    }),
    load(SETS.ui[0][0]),
  ]);
  loadEl.classList.add("gone");
  host.classList.add("ready");
  const model = gltf.scene;
  tilt.add(model);
  model.traverse((o) => {
    if (!o.isMesh) return;
    const mats = Array.isArray(o.material) ? o.material : [o.material];
    mats.forEach((m, i) => {
      let rep = null;
      if (m.name === "Glass") rep = new THREE.MeshBasicMaterial({ color: 0x050507 });
      if (m.name === "ScreenTop") rep = new THREE.MeshBasicMaterial({ map: tTop });
      if (m.name === "ScreenBottom") rep = new THREE.MeshBasicMaterial({ map: tBot });
      if (m.name === "Shell" || m.name === "Button") { m.roughness = 0.52; m.metalness = 0; }
      if (m.name === "ButtonDark") m.roughness = 0.3;
      if (rep) { rep.name = m.name; if (Array.isArray(o.material)) o.material[i] = rep; else o.material = rep; }
    });
  });
  const hinge = model.getObjectByName("LidHinge");
  S.cur = images[SETS.ui[0][0]];

  // ---------------------------------------------------------------- pose: the lid opens, then it idles; drag to turn
  const pose = { yaw: -2.4, pitch: 0.12, lid: 0, dist: 0.62 };
  const target = { yaw: -0.32, pitch: 0.02, lid: 150, dist: 0.45 };
  const drag = { on: false, x: 0, y: 0, vy: 0, vp: 0, user: 0, last: 0 };
  let intro = 0;   // 0 → 1 over the opening

  canvas.addEventListener("pointerdown", (e) => {
    drag.on = true; drag.x = e.clientX; drag.y = e.clientY; drag.vy = drag.vp = 0;
    canvas.setPointerCapture(e.pointerId);
  });
  canvas.addEventListener("pointermove", (e) => {
    if (!drag.on) return;
    const dx = e.clientX - drag.x, dy = e.clientY - drag.y;
    drag.x = e.clientX; drag.y = e.clientY;
    drag.vy = dx * 0.008; drag.vp = dy * 0.004;
    target.yaw += drag.vy; target.pitch = THREE.MathUtils.clamp(target.pitch + drag.vp, -0.35, 0.6);
    drag.last = performance.now();
  });
  const up = () => { drag.on = false; };
  canvas.addEventListener("pointerup", up);
  canvas.addEventListener("pointercancel", up);
  // vertical swipes on a phone still scroll the page: only horizontal drags turn the handheld there
  canvas.style.touchAction = "pan-y";

  function resize() {
    const w = host.clientWidth, h = host.clientHeight;
    renderer.setSize(w, h, false);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
  }
  new ResizeObserver(resize).observe(host);
  resize();

  // ---------------------------------------------------------------- loop, only while the window is on screen
  let visible = false, raf = 0, prev = performance.now(), time = 0;
  new IntersectionObserver(([e]) => {
    visible = e.isIntersecting;
    if (visible && !raf) { prev = performance.now(); raf = requestAnimationFrame(loop); }
  }, { threshold: 0.05 }).observe(host);

  function loop(now) {
    raf = 0;
    if (!visible) return;
    const dt = Math.min(0.05, (now - prev) / 1000);
    prev = now; time += dt;

    intro = reduced ? 1 : Math.min(1, intro + dt / 2.2);
    const e = 1 - Math.pow(1 - intro, 3);
    // idle sway once the intro is done and nobody has touched it for a while
    const idle = !drag.on && now - drag.last > 2500 && intro >= 1;
    if (!drag.on && !reduced) { drag.vy *= 0.92; target.yaw += drag.vy * 0.5; }
    const sway = idle && !reduced ? Math.sin(time * 0.35) * 0.0016 : 0;
    target.yaw += sway;
    const k = 1 - Math.pow(0.0015, dt);
    pose.yaw += ((intro < 1 ? -2.4 + (target.yaw + 2.4) * e : target.yaw) - pose.yaw) * (intro < 1 ? 1 : k);
    pose.pitch += (target.pitch - pose.pitch) * k;
    pose.lid = intro < 1 ? Math.max(0, (intro - 0.25) / 0.75) ** 1.5 * target.lid : target.lid;
    pose.dist = 0.62 + (target.dist - 0.62) * e;

    // the screens light up when the lid is past halfway
    if (!S.on && pose.lid > 80) { S.on = true; show(0); }
    if (S.on) {
      S.power = Math.min(1, S.power + dt * 1.6);
      S.t += dt;
      if (S.power < 1 || S.t < FADE + 0.05) S.dirty = true;
      if (S.t > SLIDE_TIME && !reduced) { S.t = -10; show(S.i + 1); }
    }
    draw();

    rig.rotation.y = pose.yaw;
    tilt.rotation.x = pose.pitch;
    rig.position.y = reduced ? 0 : Math.sin(time * 1.1) * 0.0025;
    if (hinge) hinge.rotation.x = THREE.MathUtils.degToRad(180 - pose.lid);
    const hfov = 2 * Math.atan(Math.tan(THREE.MathUtils.degToRad(camera.fov / 2)) * camera.aspect);
    const d = Math.max(pose.dist, 0.125 / Math.tan(hfov / 2));
    camera.position.set(0, d * 0.62, d * 0.8);
    camera.lookAt(0, 0.03, 0);
    renderer.render(scene, camera);
    raf = requestAnimationFrame(loop);
  }
  raf = requestAnimationFrame(loop);
}
