#pragma once
// banana/settings.conf: its defaults, its accepted values, and the card's copy made whole.
//
// The image's /etc/banana/settings.conf is the template (values and comments); this module knows the same keys so a
// card from an older Banana, or one edited by hand with a line taken out, still gets a value for every setting. It is
// deliberately the key = value format the launchers read with sed, not JSON: run-retroarch, rcS and sd2 all read
// this file, and a JSON file would need every one of them changed.
#include <map>
#include <string>

namespace banana
{
	// The card's settings over the template's: every key gets a value even when /data/banana/settings.conf is missing
	// lines (or the whole file). The card's values always win; nothing is written here.
	std::map<std::string, std::string> readSettings(const std::string& dataDir);

	// The value a key has when the card has no line for it ("" for a key the schema doesn't know).
	std::string settingDefault(const std::string& key);

	// True when settings.conf accepts value for key. A key the schema doesn't know is accepted: a setting written by
	// a newer Banana is kept rather than thrown away by an older menu.
	bool settingValid(const std::string& key, const std::string& value);

	// Which half of settings.conf an action resets. System: the menu's own keys (theme, power, network, scraper).
	// Emulator: the emulators' keys (ra_shader, ra_audio_latency, ra_vsync, nds_emulator, quick_resume, core.<system>).
	enum class ResetScope { System, Emulator };

	// True when settings.conf's key belongs to an emulator (the RetroArch cores and the emulator's own settings).
	bool settingIsEmulator(const std::string& key);

	// Put every key of the scope back to the image's template default, in place: the card's other keys, comments and
	// the user's own keys stay; in-scope keys the card lacks are added; a core.<system> with no schema entry is
	// dropped so the launcher falls back to its own default. Written beside and renamed over, only when changed.
	bool resetSettings(const std::string& dataDir, ResetScope scope);

	// First boot, and every boot after an update: the card's settings.conf with every template key it lacks added, a
	// value the schema doesn't accept put back to the template's, and duplicate keys dropped. Comments and the user's
	// own keys stay as they are. Written beside and renamed over, and only when something changed.
	// Returns true when the file was written.
	bool normalizeSettings(const std::string& dataDir);
}
