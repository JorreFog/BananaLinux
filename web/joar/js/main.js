// Joar Fogler — the page's behaviour: the boot log, the waybar, focus-follows-pointer windows, the terminal,
// and BananaOS's numbers live from the GitHub API (cached ten minutes; a snapshot shows if the API can't be reached).

const $ = (s, el = document) => el.querySelector(s);
const $$ = (s, el = document) => [...el.querySelectorAll(s)];
const REPO = "JorreFog/BananaLinux";
const API = "https://api.github.com";
const reduced = matchMedia("(prefers-reduced-motion: reduce)").matches;
const nf = new Intl.NumberFormat("sv-SE");
const fmt = (n) => nf.format(n);

// ------------------------------------------------------------------ snapshot (2 nov 2026), shown until live data arrives
const SNAPSHOT = {
  repo: { stargazers_count: 87, forks_count: 10, open_issues_count: 62, pushed_at: "2026-11-02T19:45:19Z" },
  commits: 873,
  releases: [
    ["v0.2.0", "2026-10-12", 61, 14], ["v0.3.0", "2026-10-13", 244, 48], ["v0.4.0", "2026-10-18", 171, 96],
    ["v0.4.1", "2026-10-19", 242, 102], ["v0.4.2", "2026-10-22", 818, 235], ["v0.5.0", "2026-10-28", 1075, 285],
    ["v0.5.0.1", "2026-10-28", 0, 534], ["v0.5.2.2", "2026-11-02", 0, 116],
  ].map(([tag, date, img, upd]) => ({ tag, date: date + "T12:00:00Z", img, upd, url: `https://github.com/${REPO}/releases/tag/${tag}` })),
  languages: { "C++": 2721804, Python: 649494, Shell: 550312, JavaScript: 348674, C: 248299, HTML: 132311, Makefile: 61250, Assembly: 53224, CSS: 44849 },
  log: null,
};
const data = structuredClone(SNAPSHOT);

// ------------------------------------------------------------------ boot log
const BOOT = [
  ["t", "[    0.000000] ", "Linux version 6.18.0-joar (joar@fogler) (gcc 14.2.0) #1 SMP PREEMPT"],
  ["t", "[    0.000000] ", "Command line: root=/dev/kau ro quiet splash lang=sv_SE.UTF-8"],
  ["t", "[    0.412207] ", "kau: Kandidat, visuell kommunikation och design (2015–2018) mounted"],
  ["ok", "[  OK  ] ", "Reached target Magisterexamen i projektledning."],
  ["ok", "[  OK  ] ", "Started bananaos.service — Linux för Anbernic RG DS."],
  ["ok", "[  OK  ] ", "Listening on api.github.com (stjärnor, nedladdningar, commits)."],
  ["wait", "[ .... ] ", "Starting it-sakerhetsanalytiker.service (Medieinstitutet 2026–2028)…"],
  ["ok", "[  OK  ] ", "Started Hyprland."],
  ["hl", "", "\njoar@fogler login: ▍"],
];
function boot() {
  const el = $("#boot");
  const finish = () => {
    if (el.classList.contains("done")) return;
    el.classList.add("done");
    try { sessionStorage.setItem("booted", "1"); } catch (e) {}
    start();
  };
  if (document.documentElement.classList.contains("no-boot")) return start();
  const log = $("#boot-log");
  let i = 0;
  const line = () => {
    if (el.classList.contains("done")) return;
    if (i >= BOOT.length) return setTimeout(finish, 380);
    const [cls, pre, text] = BOOT[i++];
    const p = document.createElement("span");
    p.innerHTML = `<span class="${cls === "t" ? "t" : cls}">${pre}</span>${cls === "hl" ? `<span class="hl">${text}</span>` : text}\n`;
    log.append(p);
    setTimeout(line, cls === "wait" ? 420 : 95 + Math.random() * 70);
  };
  el.addEventListener("click", finish);
  addEventListener("keydown", finish, { once: true });
  setTimeout(line, 150);
}

// ------------------------------------------------------------------ waybar: clock, workspaces, window title
function clock() {
  const t = new Date();
  const opt = { timeZone: "Europe/Stockholm" };
  $("#bar-clock").textContent = t.toLocaleTimeString("sv-SE", { ...opt, hour: "2-digit", minute: "2-digit" });
  $("#bar-date").textContent = t.toLocaleDateString("sv-SE", { ...opt, weekday: "short", day: "numeric", month: "short" });
}

function workspaces() {
  const links = $$(".workspaces a");
  const title = $("#bar-title");
  const set = (ws) => {
    links.forEach((a) => a.classList.toggle("active", a.dataset.ws === ws.dataset.ws));
    if (!focused) title.textContent = ws.dataset.title;
  };
  const io = new IntersectionObserver((entries) => {
    entries.forEach((e) => { if (e.isIntersecting) set(e.target); });
  }, { rootMargin: "-45% 0px -50% 0px" });
  $$(".ws").forEach((ws) => io.observe(ws));
  // SUPER+1…6 in Hyprland; here the bare number keys (not while typing)
  addEventListener("keydown", (e) => {
    if (e.target.closest("input, textarea") || e.metaKey || e.ctrlKey || e.altKey) return;
    const n = Number(e.key);
    if (n >= 1 && n <= 6) { e.preventDefault(); $(`#ws${n}`).scrollIntoView({ behavior: reduced ? "auto" : "smooth" }); }
  });
}

// ------------------------------------------------------------------ windows: popin on view, focus follows the pointer
let focused = null;
function focusWin(w) {
  if (focused === w) return;
  focused?.classList.remove("focus");
  focused = w;
  if (!w) return;
  w.classList.add("focus");
  $("#bar-title").textContent = w.dataset.title || w.closest(".ws")?.dataset.title || "";
}
function windows() {
  const wins = $$(".win");
  const io = new IntersectionObserver((entries) => {
    entries.filter((e) => e.isIntersecting).forEach((e, k) => {
      setTimeout(() => e.target.classList.add("in"), reduced ? 0 : k * 70);
      io.unobserve(e.target);
    });
  }, { rootMargin: "0px 0px -8% 0px", threshold: 0.05 });
  wins.forEach((w) => io.observe(w));

  const fine = matchMedia("(hover: hover) and (pointer: fine)").matches;
  if (fine) {
    wins.forEach((w) => w.addEventListener("pointerenter", () => focusWin(w)));
  } else {
    // touch: the window nearest the middle of the screen has focus
    let raf = 0;
    const pick = () => {
      raf = 0;
      const mid = innerHeight * 0.5;
      let best = null, bd = Infinity;
      for (const w of wins) {
        const r = w.getBoundingClientRect();
        if (r.bottom < 0 || r.top > innerHeight) continue;
        const d = Math.abs((r.top + r.bottom) / 2 - mid);
        if (d < bd) { bd = d; best = w; }
      }
      focusWin(best);
    };
    addEventListener("scroll", () => { if (!raf) raf = requestAnimationFrame(pick); }, { passive: true });
    pick();
  }
  wins.forEach((w) => w.addEventListener("focusin", () => focusWin(w)));
}

// ------------------------------------------------------------------ decrypt-style headings
const GLYPHS = "!<>-_\\/[]{}=+*^?#01ABCDEFJOQRX";
function scramble(el) {
  const text = el.dataset.text;
  if (reduced) { el.textContent = text; return; }
  let frame = 0;
  const total = 18 + text.length * 2;
  const tick = () => {
    let out = "";
    for (let i = 0; i < text.length; i++) {
      const reveal = (i / text.length) * total * 0.7 + 6;
      out += frame >= reveal || text[i] === " " ? text[i] : GLYPHS[(Math.random() * GLYPHS.length) | 0];
    }
    el.textContent = out;
    if (++frame <= total) requestAnimationFrame(tick); else el.textContent = text;
  };
  tick();
}
function scrambles() {
  const io = new IntersectionObserver((entries) => {
    entries.forEach((e) => { if (e.isIntersecting) { scramble(e.target); io.unobserve(e.target); } });
  }, { threshold: 0.6 });
  $$(".scramble").forEach((el) => io.observe(el));
}

// ------------------------------------------------------------------ terminal
const out = $("#term-out");
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
const print = (html, cls = "") => {
  const p = document.createElement("p");
  if (cls) p.className = cls;
  p.innerHTML = html;
  out.append(p);
  out.scrollTop = out.scrollHeight;
  return p;
};
const ART = [
  "     ██ ████████",
  "     ██ ██      ",
  "     ██ ██████  ",
  "██   ██ ██      ",
  " █████  ██      ",
].join("\n");
function fastfetch() {
  const total = data.releases.reduce((s, r) => s + r.img + r.upd, 0);
  const rows = [
    ["", `<span class="h">joar</span>@<span class="h">fogler</span>`],
    ["", `<span class="dim">───────────</span>`],
    ["OS", "Människa 1.0 (sv_SE)"],
    ["Host", "Medieinstitutet · IT-säk 2026–28"],
    ["Kernel", "Magister, projektledning (KAU)"],
    ["Modules", "Kandidat, visuell komm. (KAU)"],
    ["Packages", `${fmt(data.commits)} (git), ${fmt(total)} (dl)`],
    ["WM", "Hyprland"],
    ["Font", "JetBrains Mono"],
  ];
  const info = rows.map(([k, v]) => `<p>${k ? `<span class="k">${k}</span>: ` : ""}${v}</p>`).join("");
  const colors = ["#0a0a0b", "#ff5f6d", "#00ff99", "#f0b429", "#33ccff", "#b48cff", "#5fe1d9", "#e7e6e2"]
    .map((c) => `<i style="background:${c}"></i>`).join("");
  const div = document.createElement("div");
  div.className = "ff";
  div.innerHTML = `<pre class="ff-art">${ART}</pre><div class="ff-info">${info}<div class="ff-colors">${colors}</div></div>`;
  out.append(div);
  out.scrollTop = out.scrollHeight;
}
const go = (id) => () => { $(id).scrollIntoView({ behavior: reduced ? "auto" : "smooth" }); return `<span class="dim">→ ${id.slice(1)}</span>`; };
const COMMANDS = {
  help: () => [
    "kommandon:",
    "  <b>whoami</b>      vem är jag",
    "  <b>fastfetch</b>   systeminfo",
    "  <b>utbildning</b>  examina och studier",
    "  <b>bananaos</b>    live-siffror från GitHub",
    "  <b>projekt</b>     ls ~/projekt",
    "  <b>kontakt</b>     hur du når mig",
    "  <b>pdf</b>         spara CV:t som PDF",
    "  <b>clear</b>       rensa",
    `<span class="dim">tips: tangenterna 1–6 byter workspace</span>`,
  ].join("\n"),
  whoami: () => "Joar Fogler — IT-säkerhetsanalytiker under utbildning, projektledare och designer. Skapare av BananaOS.",
  fastfetch: () => { fastfetch(); return null; },
  neofetch: () => { fastfetch(); return null; },
  utbildning: () => [
    "<b>2026–2028</b>  IT-säkerhetsanalytiker, Medieinstitutet <span style=\"color:var(--green)\">● pågår</span>",
    "<b>Magister</b>   Projektledning, Karlstads universitet",
    "<b>2015–2018</b>  Kandidat, visuell kommunikation och design, Karlstads universitet",
  ].join("\n"),
  bananaos: () => {
    const total = data.releases.reduce((s, r) => s + r.img + r.upd, 0);
    const last = data.releases[data.releases.length - 1];
    setTimeout(go("#ws3"), 900);
    return [
      `<b>BananaOS</b> — Linux för Anbernic RG DS &amp; RG DS Plus`,
      `  nedladdningar  <b style="color:var(--banana)">${fmt(total)}</b>`,
      `  stjärnor       ${fmt(data.repo.stargazers_count)}`,
      `  commits        ${fmt(data.commits)}`,
      `  senaste        ${esc(last.tag)} (${ago(last.date)})`,
      `  <a href="https://bananaos-cfw.dev">bananaos-cfw.dev</a>`,
    ].join("\n");
  },
  projekt: () => "BananaOS/  BananaNES/  BananaGBA/  BananaSNES/  theme-studio/  save-mover/  ctf-writeups/ <span class=\"dim\">(snart)</span>",
  ls: () => "cv.pdf  kontakt.txt  om-mig.md  projekt/  utbildning/",
  "ls projekt": () => COMMANDS.projekt(),
  "cat om-mig.md": () => go("#ws2")(),
  "cat kontakt.txt": () => COMMANDS.kontakt(),
  kontakt: () => { setTimeout(go("#ws6"), 700); return `github  <a href="https://github.com/JorreFog">github.com/JorreFog</a>\ne-post  namn@exempel.se`; },
  pdf: () => { setTimeout(() => window.print(), 200); return "öppnar utskrift…"; },
  clear: () => { out.innerHTML = ""; return null; },
  exit: () => "Det finns ingen utväg. Scrolla vidare ↓",
  pwd: () => "/home/joar",
  date: () => new Date().toLocaleString("sv-SE", { timeZone: "Europe/Stockholm" }),
  hyprctl: () => "ok — men det här är en webbsida, inte din compositor 😉",
  sudo: () => `<span class="err">joar is not in the sudoers file. This incident will be reported.</span>\n<span class="dim">(least privilege — det är så man gör)</span>`,
  "sudo hire joar": () => `<span style="color:var(--green)">[sudo] lösenord godkänt.</span> Bra val. → <a href="#ws6">kontakt</a>`,
  nmap: () => "Starting Nmap… avbrutet: skanna bara system du har skriftligt tillstånd för. 🛡️",
  "rm -rf /": () => `<span class="err">rm: vägrar att arbeta rekursivt på '/'</span>\n<span class="dim">fint försök.</span>`,
  ssh: () => "ssh: connect to host joar port 22: Connection refused (prova kontakt-sidan)",
};
function run(raw) {
  const cmd = raw.trim().replace(/\s+/g, " ");
  print(`<span class="p-user">joar@fogler</span> <span class="p-path">~</span> <span class="p-sym">❯</span> ${esc(cmd)}`, "cmd");
  if (!cmd) return;
  const key = cmd.toLowerCase();
  const fn = COMMANDS[key] || (key.startsWith("sudo ") && key !== "sudo hire joar" ? COMMANDS.sudo : null)
    || (key.startsWith("nmap") ? COMMANDS.nmap : null) || (key.startsWith("cd") ? () => "<span class=\"dim\">använd siffrorna 1–6 i stället</span>" : null);
  if (!fn) return print(`<span class="err">zsh: command not found: ${esc(cmd.split(" ")[0])}</span> — skriv <b>help</b>`);
  const res = fn();
  if (res) print(res);
}
async function typeCommand(cmd) {
  const p = print(`<span class="p-user">joar@fogler</span> <span class="p-path">~</span> <span class="p-sym">❯</span> <span class="typed"></span>`, "cmd");
  const t = $(".typed", p);
  for (const ch of cmd) { t.textContent += ch; if (!reduced) await new Promise((r) => setTimeout(r, 55 + Math.random() * 50)); }
}
function terminal() {
  const form = $("#term-form"), input = $("#term-input");
  const history = [];
  let hi = 0;
  form.addEventListener("submit", (e) => {
    e.preventDefault();
    run(input.value);
    if (input.value.trim()) history.push(input.value);
    hi = history.length;
    input.value = "";
  });
  input.addEventListener("keydown", (e) => {
    if (e.key === "ArrowUp" && hi > 0) { input.value = history[--hi]; e.preventDefault(); }
    if (e.key === "ArrowDown") { hi = Math.min(history.length, hi + 1); input.value = history[hi] || ""; e.preventDefault(); }
    if (e.key === "Tab") {
      e.preventDefault();
      const m = Object.keys(COMMANDS).filter((k) => k.startsWith(input.value.toLowerCase()) && !k.includes(" "));
      if (m.length === 1) input.value = m[0]; else if (m.length) print(m.join("  "), "dim");
    }
  });
  // a tap anywhere in the terminal focuses the prompt, except on links and on touch (no surprise keyboard)
  $("#term").addEventListener("click", (e) => { if (!e.target.closest("a") && matchMedia("(pointer: fine)").matches) input.focus({ preventScroll: true }); });
  (async () => {
    await typeCommand("fastfetch");
    fastfetch();
    print(`<span class="dim">skriv <b>help</b> för kommandon</span>`);
  })();
}

// ------------------------------------------------------------------ GitHub, live
const CACHE_MS = 10 * 60 * 1000;
async function gh(path, { withLink = false } = {}) {
  const key = "gh:" + path;
  try {
    const c = JSON.parse(localStorage.getItem(key) || "null");
    if (c && Date.now() - c.t < CACHE_MS) return { json: c.json, link: c.link, cached: true };
  } catch (e) {}
  const stale = () => { try { const c = JSON.parse(localStorage.getItem(key) || "null"); if (c) return { json: c.json, link: c.link, cached: true }; } catch (e) {} return null; };
  let r;
  try { r = await fetch(API + path, { headers: { Accept: "application/vnd.github+json" } }); } catch (e) { const s = stale(); if (s) return s; throw e; }
  // rate-limited (60 an hour without a token): older numbers beat none
  if (!r.ok) { const s = stale(); if (s) return s; throw new Error(r.status + " " + path); }
  const json = await r.json();
  const link = withLink ? r.headers.get("Link") : null;
  try { localStorage.setItem(key, JSON.stringify({ t: Date.now(), json, link })); } catch (e) {}
  return { json, link, cached: false };
}
function ago(iso) {
  const s = (Date.now() - new Date(iso)) / 1000;
  const rtf = new Intl.RelativeTimeFormat("sv", { numeric: "auto" });
  if (s < 60) return "nyss";
  if (s < 3600) return rtf.format(-Math.round(s / 60), "minute");
  if (s < 86400) return rtf.format(-Math.round(s / 3600), "hour");
  if (s < 86400 * 30) return rtf.format(-Math.round(s / 86400), "day");
  return rtf.format(-Math.round(s / (86400 * 30)), "month");
}
// a monospace font gives the thousands space a whole cell; a narrow gap reads as one number
const big = (n) => fmt(n).replace(/\s/g, '<span class="th"></span>');
function countUp(el, to) {
  const from = Number(el.dataset.v ?? (el.textContent.replace(/\D/g, "") || 0));
  el.dataset.v = to;
  if (reduced || from === to) { el.innerHTML = big(to); return; }
  const t0 = performance.now(), dur = 1400;
  const step = (now) => {
    const k = Math.min(1, (now - t0) / dur);
    const e = 1 - Math.pow(1 - k, 4);
    el.innerHTML = big(Math.round(from + (to - from) * e));
    if (k < 1) requestAnimationFrame(step);
  };
  requestAnimationFrame(step);
}

function render(state) {
  const total = data.releases.reduce((s, r) => s + r.img + r.upd, 0);
  const img = data.releases.reduce((s, r) => s + r.img, 0);
  const last = data.releases[data.releases.length - 1];
  countUp($("#s-downloads"), total);
  $("#s-dl-note").textContent = `${fmt(img)} systemavbilder + ${fmt(total - img)} uppdateringar över Wi-Fi`;
  $("#s-stars").textContent = fmt(data.repo.stargazers_count);
  $("#s-forks").textContent = fmt(data.repo.forks_count);
  $("#s-commits").textContent = fmt(data.commits);
  $("#s-releases").textContent = fmt(data.releases.length);
  $("#meta-commits").textContent = fmt(data.commits);
  $("#bar-stars").textContent = fmt(data.repo.stargazers_count);
  $("#s-tag").textContent = last.tag;
  $("#s-when").textContent = ago(last.date);
  $("#s-latest").href = last.url;
  $("#stats-src").textContent = state === "live" ? "● live" : state === "cached" ? "● live (cache)" : "snapshot 2 nov";
  $("#stats-src").className = state === "snapshot" ? "" : "running";
  $("#gh-dot").className = "dot " + (state === "snapshot" ? "" : state === "live" ? "live" : "cached");
  $("#bar-gh").title = state === "snapshot" ? "GitHub kunde inte nås — visar sparade siffror" : "Live från api.github.com";
  chart();
  langs();
  cvJson(total);
  if (data.log) {
    $("#commits").innerHTML = data.log.map((c) => `<li><code>${c.sha.slice(0, 7)}</code><a href="${c.html_url}">${esc(c.commit.message.split("\n")[0])}</a><time datetime="${c.commit.committer.date}">${ago(c.commit.committer.date)}</time></li>`).join("");
  }
}

async function github() {
  render("snapshot");
  let cached = true;
  try {
    const [repo, rels, log, langsR, count] = await Promise.all([
      gh(`/repos/${REPO}`),
      gh(`/repos/${REPO}/releases?per_page=100`),
      gh(`/repos/${REPO}/commits?per_page=7`),
      gh(`/repos/${REPO}/languages`),
      gh(`/repos/${REPO}/commits?per_page=1`, { withLink: true }),
    ]);
    cached = [repo, rels, log, langsR, count].every((x) => x.cached);
    data.repo = repo.json;
    data.releases = rels.json.filter((r) => !r.draft).reverse().map((r) => ({
      tag: r.tag_name, date: r.published_at, url: r.html_url,
      img: r.assets.filter((a) => a.name.endsWith(".img.gz")).reduce((s, a) => s + a.download_count, 0),
      upd: r.assets.filter((a) => a.name.endsWith(".update.tar")).reduce((s, a) => s + a.download_count, 0),
    }));
    data.log = log.json.slice(0, 7);
    data.languages = langsR.json;
    const m = /page=(\d+)>; rel="last"/.exec(count.link || "");
    if (m) data.commits = Number(m[1]);
    render(cached ? "cached" : "live");
    if (!cached) toast(`<b>GitHub live</b> · ${fmt(data.repo.stargazers_count)} stjärnor, ${fmt(data.commits)} commits`);
  } catch (e) {
    console.warn("GitHub API:", e.message);
    render("snapshot");
  }
  // the other repos' stars, quietly
  $$(".win-proj[data-repo]").forEach(async (a) => {
    try {
      const { json } = await gh(`/repos/JorreFog/${a.dataset.repo}`);
      if (json.stargazers_count > 0) $(".proj-stars", a).textContent = `★ ${fmt(json.stargazers_count)}`;
      if (json.description) $(".proj-desc", a).title = json.description;
    } catch (e) {}
  });
}

// ------------------------------------------------------------------ cv.json, with the live numbers in it
function cvJson(total) {
  const cv = {
    namn: "Joar Fogler",
    roll: "IT-säkerhetsanalytiker (under utbildning)",
    utbildning: [
      { examen: "IT-säkerhetsanalytiker", skola: "Medieinstitutet", år: "2026–2028", status: "pågår" },
      { examen: "Magister, projektledning", skola: "Karlstads universitet" },
      { examen: "Kandidat, visuell kommunikation och design", skola: "Karlstads universitet", år: "2015–2018" },
    ],
    projekt: { bananaos: { nedladdningar: total, stjärnor: data.repo.stargazers_count, commits: data.commits, webb: "https://bananaos-cfw.dev" } },
    github: "https://github.com/JorreFog",
  };
  const json = JSON.stringify(cv, null, 2);
  $("#cv-json").innerHTML = esc(json)
    .replace(/(&quot;[^&]*?&quot;)(\s*:)/g, '<span class="jk">$1</span>$2')
    .replace(/:\s(&quot;.*?&quot;)/g, ': <span class="js">$1</span>')
    .replace(/:\s(\d+)/g, ': <span class="jn">$1</span>')
    .replace(/([{}\[\],])/g, '<span class="jp">$1</span>');
}

// ------------------------------------------------------------------ chart: downloads per release
// One series, one hue (BananaOS's yellow); value labels on the largest and the latest bar only; a tooltip per bar.
function chart() {
  const host = $("#chart");
  const rows = data.releases;
  const W = Math.max(300, host.clientWidth - 32), H = 260, pad = { l: 40, r: 6, t: 20, b: 26 };
  const max = Math.max(...rows.map((r) => r.img + r.upd));
  const nice = Math.ceil(max / 500) * 500 || 500;
  const iw = W - pad.l - pad.r, ih = H - pad.t - pad.b;
  const step = iw / rows.length, bw = Math.min(46, step * 0.62);
  const y = (v) => pad.t + ih - (v / nice) * ih;
  const ticks = [0, nice / 2, nice];
  const maxI = rows.findIndex((r) => r.img + r.upd === max), lastI = rows.length - 1;
  const bars = rows.map((r, i) => {
    const v = r.img + r.upd, x = pad.l + step * i + (step - bw) / 2, top = y(v), h = Math.max(0, pad.t + ih - top);
    const rr = Math.min(4, h, bw / 2);
    const d = h <= 0 ? "" : `M${x},${pad.t + ih} V${top + rr} Q${x},${top} ${x + rr},${top} H${x + bw - rr} Q${x + bw},${top} ${x + bw},${top + rr} V${pad.t + ih} Z`;
    const label = i === maxI || i === lastI ? `<text class="val" x="${x + bw / 2}" y="${top - 6}" text-anchor="middle">${fmt(v)}</text>` : "";
    const short = r.tag.replace(/^v/, "");
    // narrow: every other version under the axis (the tooltip and the table have them all)
    const showLabel = step >= 52 || (lastI - i) % 2 === 0;
    return `<g data-i="${i}"><path class="bar" d="${d}"/>${label}
      ${showLabel ? `<text class="axis-l" x="${x + bw / 2}" y="${H - 8}" text-anchor="middle">${short}</text>` : ""}
      <rect class="bar-hit" x="${pad.l + step * i}" y="${pad.t}" width="${step}" height="${ih + pad.b}"/></g>`;
  }).join("");
  host.innerHTML = `<svg viewBox="0 0 ${W} ${H}" role="img" aria-label="Nedladdningar per release, från ${rows[0].tag} till ${rows[lastI].tag}">
    <g class="grid">${ticks.map((t) => `<line x1="${pad.l}" x2="${W - pad.r}" y1="${y(t)}" y2="${y(t)}"/><text x="${pad.l - 8}" y="${y(t) + 3}" text-anchor="end">${t >= 1000 ? t / 1000 + "k" : t}</text>`).join("")}</g>
    <g class="axis">${bars}</g></svg><div class="tip" id="tip"></div>`;
  $$(".axis-l", host).forEach((t) => { t.setAttribute("fill", "var(--faint)"); t.style.font = "400 10.5px var(--mono)"; });
  const tip = $("#tip", host);
  const df = new Intl.DateTimeFormat("sv-SE", { day: "numeric", month: "short" });
  const show = (g) => {
    const r = rows[g.dataset.i];
    $$(".bar", host).forEach((b) => b.classList.toggle("on", b.parentNode === g));
    host.classList.add("hovering");
    tip.innerHTML = `<b>${esc(r.tag)}</b> · ${df.format(new Date(r.date))}<br><span class="tv">${fmt(r.img + r.upd)}</span> nedladdningar<br>${fmt(r.img)} avbilder · ${fmt(r.upd)} uppdateringar`;
    const hb = $(".bar-hit", g).getBoundingClientRect(), hh = host.getBoundingClientRect();
    const left = Math.min(hh.width - tip.offsetWidth - 8, Math.max(8, hb.left - hh.left + hb.width / 2 - tip.offsetWidth / 2));
    tip.style.left = left + "px";
    tip.style.top = "6px";
    tip.classList.add("on");
  };
  const hide = () => { host.classList.remove("hovering"); tip.classList.remove("on"); };
  $$("g[data-i]", host).forEach((g) => {
    g.addEventListener("pointerenter", () => show(g));
    g.addEventListener("click", () => show(g));
  });
  host.addEventListener("pointerleave", hide);
  $("#chart-table tbody").innerHTML = rows.map((r) => `<tr><td>${esc(r.tag)}</td><td>${df.format(new Date(r.date))}</td><td>${fmt(r.img + r.upd)}</td></tr>`).join("");
}

// ------------------------------------------------------------------ languages: one bar each, share of the repo's bytes
function langs() {
  const entries = Object.entries(data.languages).sort((a, b) => b[1] - a[1]);
  const total = entries.reduce((s, [, v]) => s + v, 0);
  const top = entries.slice(0, 5);
  const rest = entries.slice(5).reduce((s, [, v]) => s + v, 0);
  if (rest) top.push(["Övrigt", rest]);
  const ul = $("#langs");
  ul.innerHTML = top.map(([k, v]) => `<li><span>${esc(k)}</span><span class="lv">${(v / total * 100).toFixed(1).replace(".", ",")} %</span><span class="lb"><i data-w="${v / total * 100}"></i></span></li>`).join("");
  requestAnimationFrame(() => requestAnimationFrame(() => $$("i", ul).forEach((i) => { i.style.width = i.dataset.w + "%"; })));
}

// ------------------------------------------------------------------ themes strip
const THEMES = ["comic-panel", "coral-reef", "felt-table", "neon-arcade", "oak-bookshelf", "orbital-hud", "paper-notebook", "phosphor-terminal", "swiss-poster", "vinyl-lounge"];
function themes() {
  const title = (s) => s.split("-").map((w) => w[0].toUpperCase() + w.slice(1)).join(" ").replace("Hud", "HUD");
  $("#themes").innerHTML = THEMES.map((t) => {
    const src = `../preview/assets/screens/theme-${t}.webp`;
    return `<figure class="theme"><div class="theme-frame"><span style="background-image:url(${src})"></span><span style="background-image:url(${src})"></span></div><figcaption>${title(t)}</figcaption></figure>`;
  }).join("");
}

// ------------------------------------------------------------------ the course's progress, today
function progress() {
  const el = $("#edu-progress");
  const a = new Date(el.dataset.start), b = new Date(el.dataset.end);
  const k = Math.min(1, Math.max(0, (Date.now() - a) / (b - a)));
  const pct = Math.round(k * 100);
  $("#edu-pct").textContent = `${pct} %`;
  const io = new IntersectionObserver(([e]) => {
    if (e.isIntersecting) { $(".progress-bar i", el).style.width = Math.max(1.5, k * 100) + "%"; io.disconnect(); }
  });
  io.observe(el);
}

// ------------------------------------------------------------------ the 3D handheld, loaded when its workspace comes near
function device() {
  const host = $("#device");
  const io = new IntersectionObserver(async ([e]) => {
    if (!e.isIntersecting) return;
    io.disconnect();
    try {
      const mod = await import("./device.js");
      mod.mount(host);
    } catch (err) {
      console.warn("3D:", err);
      $("#device-load span").textContent = "3D kunde inte laddas";
    }
  }, { rootMargin: "600px 0px" });
  io.observe(host);
}

// ------------------------------------------------------------------ small things
let toastT = 0;
function toast(html) {
  const t = $("#toast");
  t.innerHTML = html;
  t.classList.add("on");
  clearTimeout(toastT);
  toastT = setTimeout(() => t.classList.remove("on"), 3200);
}
async function pageHash() {
  try {
    const html = await (await fetch(location.href.split("#")[0], { cache: "force-cache" })).text();
    const buf = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(html));
    const hex = [...new Uint8Array(buf)].map((b) => b.toString(16).padStart(2, "0")).join("");
    $("#page-hash").textContent = hex.slice(0, 16) + "…" + hex.slice(-8);
    $("#page-hash").title = hex;
  } catch (e) { $("#page-hash").textContent = "—"; }
}

// ------------------------------------------------------------------ start
let started = false;
function start() {
  if (started) return;
  started = true;
  document.body.classList.add("ready");
  windows();
  scrambles();
  terminal();
}

clock();
setInterval(clock, 15000);
workspaces();
themes();
progress();
device();
github();
pageHash();
$$("[data-print]").forEach((b) => b.addEventListener("click", () => window.print()));
addEventListener("resize", () => { clearTimeout(chart.t); chart.t = setTimeout(chart, 150); });
boot();
