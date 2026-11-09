#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace banana
{
	// Themes made by users: a .json in /data/banana/themes (or one of the image's in /usr/share/banana/themes), picked
	// in Settings > Display & sound > Theme as "user:<file name without .json>". A theme starts from Banana's dark or
	// light palette ("base") and replaces any of its colours by name ("colors") and the accent ("accent"); see
	// /usr/share/banana/themes/README.txt.

	// a theme's .json: "user:<name>" from the card or the image, "file:<path>" any file (headless runs); else ""
	std::string themePath(const std::string& value);

	struct UserTheme { std::string value, name; };	// "user:ocean", the theme's own name ("Ocean")
	std::vector<UserTheme> userThemes();				// the card's first, then the image's (a card file of the same name wins)

	// the Theme setting's value -> the palette it draws with ("dark", "light" or "carts"), with a user theme's colours
	// and accent set for it (rnds::themeColors, rnds::accent); any other theme clears them and sets accentName's accent.
	// A theme that can't be read falls back to dark, its reason in *why
	std::string applyTheme(const std::string& value, const std::string& accentName, std::string* why = nullptr);
	// a user theme ("user:<name>") that sets its own accent: Settings' Accent colour row has nothing to change then
	// (one with "accents" too lets it pick among those)
	bool themeSetsAccent(const std::string& value);
	// Banana's accent names, in Settings' order, each with its name as shown
	struct AccentName { const char* value; const char* name; };
	extern const std::vector<AccentName> ACCENT_NAMES;
	// the accents a theme gives its own shade of ("accents": { "green": "#30d158", ... }), in Banana's order: those are
	// the Accent colour row's choices with it. None: Banana's own (or, with its "accent", just that one)
	std::vector<AccentName> themeAccents(const std::string& value);
	// a theme's own picture of itself ("preview": "<file>.png" beside its .json, the top panel's home at 640x480), else ""
	std::string themePreview(const std::string& value);
	// a system's icon: the theme's own for it (themeFiles.icons by system id), else def
	std::string themedIcon(const std::string& systemId, const std::string& def);

	// a theme's "settings": { "glass": 14, "pane": "#14161e6b", "edge": "#ffffff30" }: Settings' panels frosted glass
	// over the theme's pictures (glass.h), with that tint and hairline edge (ARGB), in Inter. blur 0: as before.
	// "frost": { "top": ..., "bottom": ... }: those pictures already blurred (no blur on the handheld then)
	struct GlassLook { float blur = 0; uint32_t pane = 0x6b14161e, edge = 0x30ffffff; std::string frostTop, frostBottom; };
	extern GlassLook glassLook;
}
