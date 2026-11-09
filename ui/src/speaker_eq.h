#pragma once
// The speaker EQ's settings (Settings > Display & sound > Speaker EQ), and the profile the ALSA plugin plays.
//
// The plugin (eq/pcm_banana_eq.c, in the speakers' route that bananad writes) follows one file,
// /run/banana/speaker-eq: Equalizer APO's parametric text. banana-ui writes it from settings.conf when it starts and
// whenever the page changes something, so a change is heard at once. What is written is
//   - a profile: "tuned", the one measured for this handheld (<share>/<board>.txt, none for a board without one),
//     "none", or a file of the player's own in banana/speaker-eq on the card (AutoEq and most measurement tools
//     export this format), and
//   - the page's seven sliders (one octave each, 250 Hz to 16 kHz) on top of it,
// read with the plugin's own parser and written again, so the plugin is given only what it plays (a long card
// profile's comments can't push the sliders past what it reads). Off, or nothing to do: no file, and the plugin
// passes the sound through untouched.
#include <string>
#include <vector>

namespace banana
{
	namespace speakereq
	{
		const int BANDS = 7;
		extern const int BAND_HZ[BANDS];	// 250 ... 16000
		const int BAND_MAX_DB = 12;			// each slider -12 to +12 dB, a dB at a time

		struct State
		{
			bool on = true;
			std::string profile = "tuned";	// "tuned", "none", or a file name in banana/speaker-eq ("mine.txt")
			int gain[BANDS] = {};			// dB
		};
		struct Choice { std::string value, name; };

		// settings.conf's speaker_eq, speaker_eq_profile and speaker_eq_bands (their defaults when it has no line)
		State load(const std::string& dataDir);
		void save(const std::string& dataDir, const State& s);

		// <assets>/../speaker-eq: the profiles shipped with the image (/usr/share/banana/speaker-eq)
		std::string shareDir(const std::string& assets);

		// what Profile offers: the handheld's tuned profile ("RG DS Plus Tuned", when <shareDir>/<board>.txt is there), None, then the card's
		// .txt files in banana/speaker-eq, by name. A chosen file no longer on the card is listed too, as missing.
		std::vector<Choice> profiles(const std::string& dataDir, const std::string& shareDir, const std::string& current);

		// a profile's name on screen ("RG DS Plus Tuned", "None", "Mine", "Mine (not on the card)"), without listing the
		// card's folder; and whether any slider moves it
		std::string profileName(const std::string& value, const std::string& dataDir, const std::string& shareDir);
		bool adjusted(const State& s);

		// the profile's own text, as its file has it ("" for none, or a file that isn't there)
		std::string profileText(const State& s, const std::string& dataDir, const std::string& shareDir);
		// the text the plugin is given ("" when there is nothing to do: off, or no profile and every slider at 0), from
		// the profile's text (one read of its file for several States)
		std::string compose(const State& s, const std::string& profile);
		std::string compose(const State& s, const std::string& dataDir, const std::string& shareDir);
		// what settings.conf keeps, given to the plugin (the menu's start, and Reset system settings)
		bool applySaved(const std::string& dataDir, const std::string& shareDir);

		// /run/banana/speaker-eq (BANANA_SPEAKER_EQ in the host tests): written beside and renamed over, so the plugin
		// never reads half of it; "" removes it. False when it couldn't be written.
		std::string livePath();
		bool apply(const std::string& text);

		// the curve the text makes, in dB, at each of freqs (the filters' shape: the preamp and headroom left out)
		void curve(const std::string& text, const std::vector<double>& freqs, std::vector<double>& db);
	}
}
