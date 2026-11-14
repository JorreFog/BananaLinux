#pragma once
// banana-ui on Android: how a game starts. A system's games are opened by another app (an emulator) through an
// intent, and the launch table says which: android/system/launch.conf, one [section] per system id of systems.conf
// (buildroot/board/thor/systems.conf).
//
//   [gba]
//   package  = com.retroarch.aarch64                 the emulator's package (required); banana:<page> is one of the
//                                                    launcher's own pages, none hides the system's shelf on Android
//   activity = com.retroarch.browser.retroactivity.RetroActivityFuture   (optional: the package's own pick otherwise)
//   action   = android.intent.action.VIEW            (the default)
//   data     = none | file | content | <URI template>  the intent's data: nothing, the ROM's file:// URI, a content://
//                                                    URI of the ROM from the launcher's provider, or a URI with the
//                                                    placeholders below in it (scummvm:%ROMTEXT%) (the default: none)
//   mime     = application/octet-stream              (optional) the data's type
//   flags    = FLAG_ACTIVITY_NEW_TASK FLAG_ACTIVITY_CLEAR_TASK   (optional) Intent flag names, space separated
//   core     = mgba                                  the RetroArch core, for %CORE% (settings.conf's core.<id> wins)
//   extra.ROM = %ROM%                                any number of string extras; %ROM% the ROM's path, %ROMURI% its
//   extra.LIBRETRO = .../cores/%CORE%_libretro_android.so         content URI, %CORE% the core, %ROMTEXT% the ROM
//                                                    file's first non-empty line, %ROMKEY:<k>% the value of "k = v"
//                                                    in the ROM file (a PC game's .pc: steam = 1145360)
//   [gba.<variant>]                                  another emulator for the same system: settings.conf's
//                                                    launch.<id> = <variant> picks it (the plain [gba] otherwise)
//
// Tables are read in layers, a later one's section replacing an earlier one's whole: the copy inside the APK, then
// /vendor/etc/banana/launch.conf, /system_ext/etc/banana/launch.conf (the ROM's), then banana/launch.conf on the
// card (the player's own). Pure C++ with no Android in it: tests/launch-test (android/launcher/tests) runs it on a PC.
#include <map>
#include <string>
#include <vector>

namespace banana
{
	namespace launch
	{
		struct Entry
		{
			std::string system;		// the section's name: a systems.conf id
			std::string package, activity, action = "android.intent.action.VIEW", data = "none", mime, core;
			std::vector<std::string> flags;
			std::vector<std::pair<std::string, std::string>> extras;	// in the file's order
			std::string file;		// the table it came from (the log says which)
		};

		class Table
		{
		public:
			// one file's sections over the ones read so far (a section replaces the same system's whole); false when
			// the file isn't there. Problems found are added to warnings ("<file>:<line>: <what>")
			bool load(const std::string& path);
			// the files of the layers above, in order; the number of files read
			int loadAll(const std::string& apkCopy, const std::string& cardCopy);
			// the system's entry: its [<id>.<variant>] when variant names one, else its [<id>]
			const Entry* find(const std::string& system, const std::string& variant = "") const;
			// package = none: the system isn't offered on Android
			bool hidden(const std::string& system) const;
			size_t size() const { return entries.size(); }
			std::vector<std::string> warnings;

		private:
			std::map<std::string, Entry> entries;
		};

		// the core a system's RetroArch games use: the card's choice (settings.conf's core.<id>) when there is one, else
		// the table's (core =), else the one its systems.conf run line names (run-retroarch <core> %ROM% ...)
		std::string coreOf(const std::string& run, const std::string& chosen, const std::string& table = "");

		// the launcher's own page an entry names (package = banana:<page>), or ""
		std::string pageOf(const Entry& e);

		// what the Java side turns into an Intent: "key=value" lines (package, activity, action, data, mime, flags,
		// rom, extra.<name>), the placeholders put in but %ROMURI% (the Java side's: it makes the content URI)
		std::vector<std::string> intentSpec(const Entry& e, const std::string& rom, const std::string& core);

		// the placeholders replaced in one value (%ROMTEXT% and %ROMKEY:<k>% read from the ROM file)
		std::string expand(const std::string& v, const std::string& rom, const std::string& core);

		// a systems.conf with the sections of the systems the table hides (package = none) left out: what the Android
		// menu reads. False when it couldn't be written
		bool filterSystems(const Table& t, const std::string& from, const std::string& to);
	}
}
