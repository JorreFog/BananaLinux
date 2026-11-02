// Joar Fogler — CV. A quiet fade as sections come into view, BananaOS's numbers live from the GitHub API
// (cached ten minutes; older numbers or a snapshot if the API can't be reached), and the 3D handheld,
// loaded only when the project section comes near.

const $ = (s, el = document) => el.querySelector(s);
const $$ = (s, el = document) => [...el.querySelectorAll(s)];
const REPO = "JorreFog/BananaLinux";
const API = "https://api.github.com";
const reduced = matchMedia("(prefers-reduced-motion: reduce)").matches;
const fmt = (n) => new Intl.NumberFormat("sv-SE").format(n);

// ------------------------------------------------------------------ sections fade in, the nav marks where you are
const io = new IntersectionObserver((entries) => {
  entries.forEach((e) => { if (e.isIntersecting) { e.target.classList.add("in"); io.unobserve(e.target); } });
}, { rootMargin: "0px 0px -10% 0px" });
$$(".reveal").forEach((el) => io.observe(el));

const navLinks = $$(".nav a");
const navIO = new IntersectionObserver((entries) => {
  entries.forEach((e) => {
    if (e.isIntersecting) navLinks.forEach((a) => a.classList.toggle("on", a.getAttribute("href") === "#" + e.target.id));
  });
}, { rootMargin: "-40% 0px -55% 0px" });
$$("section[id]").forEach((s) => navIO.observe(s));

// ------------------------------------------------------------------ the course's progress, today
{
  const el = $("#edu-progress");
  const a = new Date(el.dataset.start), b = new Date(el.dataset.end);
  const k = Math.min(1, Math.max(0, (Date.now() - a) / (b - a)));
  const pio = new IntersectionObserver(([e]) => {
    if (e.isIntersecting) { $("i", el).style.width = Math.max(2, k * 100) + "%"; pio.disconnect(); }
  });
  pio.observe(el);
}

// ------------------------------------------------------------------ GitHub, live
const CACHE_MS = 10 * 60 * 1000;
async function gh(path) {
  const key = "gh:" + path;
  let c = null;
  try { c = JSON.parse(localStorage.getItem(key) || "null"); } catch (e) {}
  if (c && Date.now() - c.t < CACHE_MS) return { ...c, cached: true };
  let r;
  try { r = await fetch(API + path, { headers: { Accept: "application/vnd.github+json" } }); } catch (e) { if (c) return { ...c, cached: true }; throw e; }
  // rate-limited (60 an hour without a token): older numbers beat none
  if (!r.ok) { if (c) return { ...c, cached: true }; throw new Error(r.status + " " + path); }
  const out = { t: Date.now(), json: await r.json(), link: r.headers.get("Link") };
  try { localStorage.setItem(key, JSON.stringify(out)); } catch (e) {}
  return { ...out, cached: false };
}

async function github() {
  try {
    const [repo, rels, count] = await Promise.all([
      gh(`/repos/${REPO}`),
      gh(`/repos/${REPO}/releases?per_page=100`),
      gh(`/repos/${REPO}/commits?per_page=1`),
    ]);
    // downloads: the card images and the Wi-Fi updates, not the checksum files
    const total = rels.json.reduce((s, r) => s + r.assets
      .filter((a) => a.name.endsWith(".img.gz") || a.name.endsWith(".update.tar"))
      .reduce((t, a) => t + a.download_count, 0), 0);
    const latest = rels.json.find((r) => !r.draft);
    $("#s-downloads").textContent = fmt(total);
    $("#s-stars").textContent = fmt(repo.json.stargazers_count);
    const m = /page=(\d+)>; rel="last"/.exec(count.link || "");
    if (m) $("#s-commits").textContent = fmt(Number(m[1]));
    if (latest) $("#s-tag").textContent = latest.tag_name;
    $("#gh-dot").classList.add("live");
    const t = new Date(Math.min(repo.t, rels.t, count.t));
    $("#s-src").textContent = `Live från GitHub · hämtat ${t.toLocaleTimeString("sv-SE", { hour: "2-digit", minute: "2-digit" })}`;
  } catch (e) {
    console.warn("GitHub API:", e.message);
    $("#s-src").textContent = "Siffror från GitHub, 2 nov 2026";
  }
}
github();

// ------------------------------------------------------------------ the 3D handheld
const device = $("#device");
const dio = new IntersectionObserver(async ([e]) => {
  if (!e.isIntersecting) return;
  dio.disconnect();
  try { (await import("./device.js")).mount(device); }
  catch (err) { console.warn("3D:", err); $("#device-load span").textContent = "3D-modellen kunde inte laddas"; }
}, { rootMargin: "500px 0px" });
dio.observe(device);

$$("[data-print]").forEach((b) => b.addEventListener("click", () => window.print()));
