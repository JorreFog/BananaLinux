// Banana Theme Studio: the editor's own description of a layout theme.
//
// This is the field metadata the inspector, pickers and autocomplete are built from — what kinds of value a
// property takes, which properties belong to which element type, and the hooks an expression can read. It is
// derived from docs/THEMES.md and ui/src/layout.cpp (hook(), sysField(), gameField()) and is kept next to the
// machine-readable docs/theme.schema.json, which is the format's validation/interchange artifact for other tools.
//
// The palettes, accents, colour keys and sound names are the same ones system/rootfs/usr/share/banana/themes/
// theme-builder.html uses, so a layout theme's Settings/keyboard colours are edited the same way.
(function (global) {
  "use strict";

  const VIEWS = ["home", "library", "launch", "all"];
  const SCREENS = ["top", "bottom"];

  // ---- the palettes (ui/rnds/RndsUI.cpp) and the accents, as theme-builder.html has them -----------------
  const DARK = { ink: "#f1ede2", muted: "#a39d8e", line: "#8a8374", edge: "#e8e2d3", navy: "#fff6dc", red: "#ff3b4a",
    shade40: "#00000066", shade45: "#00000073", shade35: "#00000059", blackish: "#0b0a0d",
    screenBg: "#17161c", screenFrame: "#0b0a0d", screenBand: "#2e2b33", ditherA: "#ffffff24", ditherB: "#ffffff12",
    sheetBase: "#15141a", sheetHi: "#ffffff1a", sheetLo: "#00000059",
    panelBand: "#3a3540", panelBody: "#17161c", panelLow: "#0b0a0d", chipBand: "#2e2b33", chipBody: "#121117",
    frameBorder: "#e8e2d3", frameBg: "#17161c", meterBg: "#15141a", brandBg: "#262219", kbdBg: "#f1ede2", kbdInk: "#17161c",
    rule: "#45403d", bubbleBand: "#38333b", bubbleBody: "#191820", bubbleLow: "#0f0e13",
    trackBg: "#100f14", trackHi: "#353039", trackShade: "#00000073", slotOn: "#ffffff", railBg: "#100f14", faceMuted: "#45403d",
    favBorder: "#ff3b4a", favBand: "#5a2430", favBody: "#2a1016", favInk: "#ff8a96", glint: "#ffffff47", floor: "#00000059",
    slotDark: "#070609", slotLip: "#45403d", cartLabel: "#f1ede2", onPick: "#17161c", onPickAccent: "#7a3a00" };
  const LIGHT = { ink: "#2a2620", muted: "#7a7262", line: "#a69d8c", edge: "#3a342c", navy: "#fffaf0", red: "#e23b4a",
    shade40: "#3a2e1438", shade45: "#3a2e1440", shade35: "#3a2e1424", blackish: "#3a2e144d",
    screenBg: "#f3efe6", screenFrame: "#d4ccbd", screenBand: "#fdfcf9", ditherA: "#3a2e1412", ditherB: "#3a2e1409",
    sheetBase: "#fdfbf6", sheetHi: "#ffffff8c", sheetLo: "#3a2e141f",
    panelBand: "#ffffff", panelBody: "#f6f2ea", panelLow: "#d8d0c0", chipBand: "#ffffff", chipBody: "#f6f2ea",
    frameBorder: "#3a342c", frameBg: "#ebe5d9", meterBg: "#e3dccd", brandBg: "#262219", kbdBg: "#3a342c", kbdInk: "#f2f6fa",
    rule: "#d6cfc0", bubbleBand: "#ffffff", bubbleBody: "#faf7f0", bubbleLow: "#ddd5c6",
    trackBg: "#e6dfd0", trackHi: "#ffffff", trackShade: "#3a2e142e", slotOn: "@accent", railBg: "#e6dfd0", faceMuted: "#d6cfc0",
    favBorder: "#e23b4a", favBand: "#fff2f3", favBody: "#ffdfe3", favInk: "#c8283a", glint: "#ffffff73", floor: "#3a2e1433",
    slotDark: "#2a2620", slotLip: "#a69d8c", cartLabel: "#ffffff", onPick: "#17161c", onPickAccent: "#7a3a00" };
  const ACCENTS = { yellow: ["#f0b429", "#ffe68a", "#8a5a00", "#ffcf5c", "#c98f12"],
    blue: ["#4a9fe0", "#b7e1ff", "#0f3f66", "#7cc0f2", "#2f7fc0"],
    red: ["#e8574d", "#ffb8b0", "#7a1a14", "#f58a80", "#c23b32"],
    green: ["#56bf6b", "#bff0c8", "#1d5a2b", "#86d896", "#3d9a52"],
    purple: ["#a07ae6", "#ddd0ff", "#43266e", "#bfa4f2", "#7f58c4"],
    pink: ["#ec6fa8", "#ffc6e0", "#7a1f4a", "#f59cc4", "#c9508a"] };
  const ACCENT_SHADES = ["base", "light", "dark", "band", "low"];
  const COLOR_GROUPS = [
    ["The screen", ["screenBg", "screenBand", "screenFrame", "ditherA", "ditherB", "rule"]],
    ["Text", ["ink", "muted", "line", "edge", "navy", "red"]],
    ["Panels", ["panelBand", "panelBody", "panelLow", "chipBand", "chipBody", "bubbleBand", "bubbleBody", "bubbleLow"]],
    ["The shelf", ["sheetBase", "sheetHi", "sheetLo", "trackBg", "trackHi", "trackShade", "railBg", "slotOn", "faceMuted", "floor", "glint"]],
    ["Box art", ["frameBorder", "frameBg", "slotDark", "slotLip", "cartLabel"]],
    ["Favourites", ["favBorder", "favBand", "favBody", "favInk"]],
    ["Other", ["meterBg", "brandBg", "kbdBg", "kbdInk", "blackish", "shade35", "shade40", "shade45"]],
    ["Settings", ["onPick", "onPickAccent"]],
  ];
  const COLOR_KEYS = COLOR_GROUPS.flatMap((g) => g[1]);
  const SOUNDS = ["scroll", "select", "back", "fav", "ready", "insert"];
  const BUILTIN_FONTS = ["pixel", "pixel-medium", "inter", "inter-medium", "inter-semibold", "inter-bold"];

  // ---- buttons and the actions a view can bind ------------------------------------------------------------
  const BUTTONS = ["left", "right", "up", "down", "a", "b", "x", "y", "l", "r"];
  const BUTTON_ACTIONS = [["prev", "the previous item"], ["next", "the next item"], ["prev:N", "back N items"],
    ["next:N", "forward N items"], ["first", "the first item"], ["last", "the last item"], ["open", "open the system / play the game"],
    ["back", "leave the library"], ["fav", "star the selection"], ["resume", "the system's last game"],
    ["prevSystem", "the previous system"], ["nextSystem", "the next system"], ["none", "nothing"]];

  // ---- elements and their properties ----------------------------------------------------------------------
  // kind: number, length (a number or "N%"), color, text (any value: literal/expr/template), bool,
  //       enum (with options), component (use/with), tree (children/item), object (states/smooth/shadow).
  const COMMON = [
    ["x", "length"], ["y", "length"], ["w", "length"], ["h", "length"],
    ["right", "length"], ["bottom", "length"],
    ["anchor", "enum", ["center", "top", "top left", "top right", "bottom", "bottom left", "bottom right", "left", "right"]],
    ["ax", "number"], ["ay", "number"],
    ["scale", "number"], ["opacity", "number"], ["visible", "bool"], ["if", "bool"], ["clip", "bool"],
  ];
  const APPEARANCE = [
    ["states", "object"], ["smooth", "object"], ["onTap", "enum", BUTTONS],
  ];
  const TYPE_PROPS = {
    rect: [["fill", "color"], ["color", "color"], ["gradient", "object"], ["radius", "length"],
      ["border", "any"], ["borderColor", "color"], ["shadow", "object"]],
    text: [["text", "text"], ["font", "enum", function (fonts) { return BUILTIN_FONTS.concat(fonts || []); }],
      ["size", "number"], ["color", "color"], ["align", "enum", ["left", "center", "right"]],
      ["valign", "enum", ["top", "middle", "bottom"]], ["upper", "bool"], ["spacing", "number"],
      ["wrap", "bool"], ["maxLines", "number"], ["lineHeight", "number"], ["ellipsis", "bool"],
      ["shadow", "object"], ["outline", "object"]],
    image: [["src", "text"], ["fallback", "text"], ["fit", "enum", ["cover", "contain", "fill", "coverTop", "pixel", "pixelContain"]],
      ["radius", "length"], ["tint", "color"], ["crop", "enum", ["cardLabel", "noStrip"]], ["placeholder", "color"]],
    group: [["children", "tree"], ["layout", "enum", ["row", "column"]], ["gap", "number"], ["padding", "number"],
      ["justify", "enum", ["start", "center", "end", "between"]], ["align", "enum", ["start", "center", "end"]],
      ["fill", "color"], ["gradient", "object"], ["radius", "length"], ["border", "any"], ["borderColor", "color"], ["shadow", "object"]],
    list: [["source", "enum", ["systems", "games", "hints", "range"]], ["direction", "enum", ["row", "column", "grid", "free"]],
      ["columns", "number"], ["itemW", "number"], ["itemH", "number"], ["gap", "number"], ["stepX", "number"], ["stepY", "number"],
      ["scrollMode", "enum", ["center", "edge", "none"]], ["loop", "bool"], ["flow", "bool"], ["range", "number"],
      ["selectedOnTop", "bool"], ["item", "tree"]],
  };
  const TYPE_LABELS = { rect: "Rect", text: "Text", image: "Image", group: "Group", list: "List" };

  // ---- hooks: everything an expression can read (layout.cpp hook(), sysField(), gameField()) ---------------
  const SYS_FIELDS = ["id", "name", "maker", "color", "accent", "icon", "games", "played", "time", "fav", "app",
    "hasResume", "resumeTitle", "resumeEyebrow", "resumeBox", "resumeCart", "resumeLabel", "resumeArt", "index"];
  const GAME_FIELDS = ["title", "sub", "plays", "last", "time", "box", "cart", "label", "art", "rom", "romIcon", "icon",
    "achA", "achB", "achLabel", "achProgress", "fav", "index", "systemId", "systemName"];
  const HOOKS = [
    { group: "The view", names: ["view", "W", "H", "screen"] },
    { group: "Time", names: ["time", "clock", "date", "moved", "entered", "dir"] },
    { group: "Status", names: ["battery", "charging", "batteryMode", "online", "motion", "brand"] },
    { group: "Colour", names: ["accent", "accent.light", "accent.dark", "accent.band", "accent.low", "accent.ink", "accent.inkSub", "palette.<name>"] },
    { group: "The list", names: ["cursor", "count", "scroll", "systems.count", "games.count"] },
    { group: "The launch view", names: ["launch.progress", "launch.t"] },
    { group: "An empty library", names: ["empty", "empty.title", "empty.hint"] },
    { group: "The selected system (sys.*)", names: SYS_FIELDS.map((f) => "sys." + f) },
    { group: "The selected game (game.*)", names: GAME_FIELDS.map((f) => "game." + f) },
    { group: "Inside a list item", names: ["item.<sys or game field>", "item.key", "item.label", "i", "n", "sel", "rel", "pos", "first", "last", "ix", "iy"] },
  ];
  const HOOK_NAMES = HOOKS.flatMap((h) => h.names);

  // statement-level members, for the summary panel
  const BEHAVIOR_KEYS = ["launchMs", "scrollMs", "settleMs", "fps", "wrap", "extraHints"];

  const api = { VIEWS, SCREENS, DARK, LIGHT, ACCENTS, ACCENT_SHADES, COLOR_GROUPS, COLOR_KEYS, SOUNDS,
    BUILTIN_FONTS, BUTTONS, BUTTON_ACTIONS, COMMON, APPEARANCE, TYPE_PROPS, TYPE_LABELS,
    SYS_FIELDS, GAME_FIELDS, HOOKS, HOOK_NAMES, BEHAVIOR_KEYS };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  global.BananaSchema = api;
})(typeof globalThis !== "undefined" ? globalThis : this);