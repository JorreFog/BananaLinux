#pragma once
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
	bool themeSetsAccent(const std::string& value);
	// a theme's own picture of itself ("preview": "<file>.png" beside its .json, the top panel's home at 640x480), else ""
	std::string themePreview(const std::string& value);
	// a system's icon: the theme's own for it (themeFiles.icons by system id), else def
	std::string themedIcon(const std::string& systemId, const std::string& def);
}
