// Banana Theme Studio: the preview client.
//
// A theme is previewed by the real engine, not a re-implementation: this posts the theme to serve.py, which
// writes it to a file and runs `banana-ui --headless ... --theme <file> --script "<commands>"`, returning the
// PNGs it drew and the `banana-ui: theme: ...` lines it printed. Exact, and it drifts only when the engine
// does. When there is no server (the page opened from file://) previews are simply unavailable and the editor
// keeps working.
(function (global) {
  "use strict";

  const DEFAULT_SCRIPT = "home 0; wait 900; shot home; press right; wait 700; shot home2; press a; wait 900; shot lib; press down; wait 600; shot lib2; press a; wait 400; shot launch";

  const BananaRender = {
    available: false,
    engine: "",
    detail: "",

    async check() {
      try {
        const r = await fetch("api/health", { cache: "no-store" });
        if (!r.ok) throw new Error("HTTP " + r.status);
        const j = await r.json();
        this.available = !!j.ok;
        this.engine = j.engine || "";
        this.detail = j.detail || "";
      } catch (e) {
        this.available = false;
        this.detail = "no preview server (open the page with tools/theme-studio/serve.py for exact previews)";
      }
      return this.available;
    },

    // -> { shots: [{ name, top, bot }], log: string, errors: string[] }
    async render(theme, script, size) {
      const body = JSON.stringify({ theme: theme, script: script || DEFAULT_SCRIPT, size: size || "640x480" });
      const r = await fetch("api/render", { method: "POST", headers: { "content-type": "application/json" }, body: body });
      if (!r.ok) {
        let msg = "render failed (HTTP " + r.status + ")";
        try { const j = await r.json(); if (j && j.error) msg = j.error; } catch (e) { /* ignore */ }
        throw new Error(msg);
      }
      return r.json();
    },
  };

  if (typeof module !== "undefined" && module.exports) module.exports = BananaRender;
  global.BananaRender = BananaRender;
})(typeof globalThis !== "undefined" ? globalThis : this);