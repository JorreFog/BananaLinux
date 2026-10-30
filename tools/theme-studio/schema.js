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
  // the systems the menu ships (system/rootfs/etc/banana/systems.conf); the server can add a card's own
  const SYSTEM_IDS = ["nds", "gba", "gbc", "gb", "nes", "snes", "megadrive", "psx", "n64",
    "dodakvarter", "rocknixdsbank", "pico-8", "ports", "files"];
  const BUILTIN_FONTS = ["pixel", "pixel-medium", "inter", "inter-medium", "inter-semibold", "inter-bold"];

  // ---- buttons and the actions a view can bind ------------------------------------------------------------
  const BUTTONS = ["left", "right", "up", "down", "a", "b", "x", "y", "l", "r"];
  const BUTTON_ACTIONS = [["prev", "the previous item"], ["next", "the next item"], ["prev:3", "back 3 items (a grid's row)"],
    ["next:3", "forward 3 items"], ["prev:5", "back 5 items"], ["next:5", "forward 5 items"], ["prev:10", "back 10 items"], ["next:10", "forward 10 items"], ["first", "the first item"], ["last", "the last item"], ["open", "open the system / play the game"],
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
      ["scrollMode", "enum", ["center", "edge", "none"]], ["loop", "bool"], ["flow", "bool"], ["range", "number"], ["count", "number"],
      ["selectedOnTop", "bool"], ["item", "tree"]],
  };
  const TYPE_LABELS = { rect: "Rectangle", text: "Text", image: "Picture", group: "Group", list: "List" };

  // ---- what the inspector calls each property, and a plain-words explanation ---------------------------------
  // Keyed by the JSON property name, so every field gets a readable label and a hint without the tables above
  // carrying two more columns. The wording matches the desktop studio's field tips (widgets.py).
  const FIELD_LABELS = {
    x: "X", y: "Y", w: "Width", h: "Height", right: "Right", bottom: "Bottom",
    anchor: "Anchor", ax: "Anchor X", ay: "Anchor Y", scale: "Scale",
    opacity: "Opacity", visible: "Visible", if: "If", clip: "Clip children", onTap: "Tap acts as",
    states: "Changes when…", smooth: "Ease smoothly",
    fill: "Fill", color: "Colour", gradient: "Gradient", radius: "Corner radius",
    border: "Border", borderColor: "Border colour", shadow: "Shadow",
    text: "Text", font: "Font", size: "Size", align: "Align", valign: "Vertical align",
    upper: "Capitals", spacing: "Letter spacing", wrap: "Wrap", maxLines: "Max lines",
    lineHeight: "Line height", ellipsis: "Ellipsis",
    outline: "Outline", width: "Width", blur: "Blur", spread: "Spread",
    src: "Source", fallback: "Fallback", fit: "Fit", tint: "Tint", crop: "Crop", placeholder: "Placeholder",
    children: "Children", layout: "Layout", gap: "Gap", padding: "Padding",
    justify: "Justify", source: "Shows", direction: "Direction", columns: "Columns",
    itemW: "Item width", itemH: "Item height", stepX: "Step X", stepY: "Step Y",
    scrollMode: "Scroll", loop: "Loop", flow: "Flow", range: "Range", count: "Count",
    selectedOnTop: "Selected on top", item: "Item",
    name: "Name", author: "Author", debug: "Show problems on screen",
    use: "Use a component", with: "Values to pass in",
  };
  const FIELD_TIPS = {
    x: "Distance from the left of the box it sits in. The screen is 640 wide, 480 tall.",
    y: "Distance from the top.",
    w: "The width. Leave it empty to fill the box it sits in.",
    h: "The height. Leave it empty to fill the box it sits in.",
    right: "Distance from the right edge, instead of X.",
    bottom: "Distance from the bottom edge, instead of Y.",
    anchor: "Which point of the element X and Y place.",
    ax: "0 = its left edge, 1 = its right edge.",
    ay: "0 = its top edge, 1 = its bottom edge.",
    scale: "Grows or shrinks it around its anchor.",
    opacity: "0 to 1. Multiplied into everything it holds.",
    visible: "Off hides it. Usually an expression, e.g. \"=sel\" or \"=battery >= 0\".",
    if: "Same as Visible: a plain yes or no.",
    clip: "Cut off whatever sticks out of its box.",
    onTap: "The button this element acts as when it is touched.",
    states: "Properties that take over while a condition holds — a selected shoe, a favourite star.",
    smooth: "Ease towards new values instead of jumping to them (time constants in ms).",
    fill: "A colour, or an expression like \"=sel ? navy : face\".",
    gradient: "A colour ramp. Replaces the fill.",
    radius: "A number, \"50%\" for a pill or a circle, or four corners [top-left, top-right, bottom-right, bottom-left].",
    border: "How thick the outline is, drawn inside the box.",
    borderColor: "The outline's colour.",
    shadow: "A soft dark copy behind it. Drawn when a colour is set.",
    text: "The words. {curly braces} drop a value in: \"{game.title}\".",
    font: "One of the built-in fonts, or a name from the theme's fonts.",
    size: "In screen units — the screen is 480 tall.",
    align: "For text: within its width. For a row or column: across it.",
    valign: "Within its height.",
    upper: "Draw the text in CAPITALS.",
    spacing: "Space between the letters.",
    wrap: "Break long lines instead of letting them run off.",
    maxLines: "Stop after this many lines.",
    lineHeight: "Space between the lines.",
    ellipsis: "End a line that is too long with … (on by default).",
    src: "A picture beside the theme (\"my-theme/cover.png\"), or a hook: {sys.icon}, {game.art}.",
    fallback: "Used when the source is empty or will not load.",
    fit: "How the picture fills its box: cover, contain, fill, pixel…",
    tint: "Multiplied into the picture's colours.",
    crop: "Trim a real DS card scan's white strip.",
    placeholder: "Shown while the picture loads, or when there is nothing to show.",
    children: "The elements inside it. Add and reorder them in the Layers tree.",
    layout: "Place the children one after another, in a row or a column.",
    gap: "Space between them.",
    padding: "Space inside the edge.",
    justify: "Where they sit along the row or column.",
    source: "What the list repeats: your systems, the games in a library, the on-screen button hints, or simply N items.",
    direction: "How the repeats are laid out. \"free\" lets each one place itself from pos/rel (a wheel, a fan).",
    columns: "How many columns a grid has.",
    itemW: "The size of one repeat.",
    itemH: "The size of one repeat.",
    stepX: "Space between repeats (default: the item width + gap).",
    stepY: "Space between repeats (default: the item height + gap).",
    scrollMode: "\"center\" keeps the selection in the middle, \"edge\" scrolls only when it would leave, \"none\" never scrolls.",
    loop: "Start again at the other end instead of stopping.",
    flow: "Each item as big as its own content (the hint chips).",
    range: "A free list draws this many items either side of the selection.",
    count: "How many items a \"range\" list makes.",
    selectedOnTop: "Draw the selected one last, over its neighbours.",
    item: "The element that is repeated: write it once and the menu repeats it for every system or game.",
    name: "What Settings shows in the theme list.",
    author: "Your name. Kept in the file so people know who made it.",
    debug: "Paints each problem in red on the top screen while you work. Turn it off when you are done.",
    use: "Drop in one of the reusable components instead of writing the element again.",
  };

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
    FIELD_LABELS, FIELD_TIPS, SYS_FIELDS, GAME_FIELDS, HOOKS, HOOK_NAMES, BEHAVIOR_KEYS, SYSTEM_IDS };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  global.BananaSchema = api;
})(typeof globalThis !== "undefined" ? globalThis : this);