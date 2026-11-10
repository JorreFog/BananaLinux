#pragma once
// banana-ui: System > Parental controls. While on, the menu shows only the games a parent allowed, and Settings and a
// game's own page (X) ask for the PIN first; the volume and brightness keys stay. Kept on the card:
//   settings.conf     parental = on | off
//   parental.conf     pin = <SHA-256 of salt and PIN, hex>, salt = <hex> (never the PIN itself: it may be one used elsewhere)
//   parental-games.txt  the games allowed, "<system>/<rel>" a line (the favourites' keys)
// Anyone with the card in a computer can still turn it off: it keeps a child in the games, it isn't a safe.
#include <set>
#include <string>

namespace banana
{
	namespace parental
	{
		bool on(const std::string& dataDir);				// settings.conf's parental = on, and a PIN set
		bool hasPin(const std::string& dataDir);
		bool checkPin(const std::string& dataDir, const std::string& pin);
		bool setPin(const std::string& dataDir, const std::string& pin);	// a new salt each time
		std::set<std::string> games(const std::string& dataDir);
		bool saveGames(const std::string& dataDir, const std::set<std::string>& keys);
		// the daily play time (parental_minutes, bananad counts it in playtime.txt): the seconds left today, or -1 with
		// no limit (the controls off, no PIN, or no minutes set)
		long playLeft(const std::string& dataDir);
		std::string sha256Hex(const std::string& data);	// (the test checks it against known values)
		// a game for the Allowed games page: its system's name, its key ("<system>/<rel>") and its title
		struct GameRef { std::string system, key, title; };
	}
}
