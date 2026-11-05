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

		// what Profile offers: Tuned for this handheld (when <shareDir>/<board>.txt is there), None, then the card's
		// .txt files in banana/speaker-eq, by name. A chosen file no longer on the card is listed too, as missing.
		std::vector<Choice> profiles(const std::string& dataDir, const std::string& shareDir, const std::string& current);

		// the text the plugin is given ("" when there is nothing to do: off, or no profile and every slider at 0)
		std::string compose(const State& s, const std::string& dataDir, const std::string& shareDir);

		// /run/banana/speaker-eq (BANANA_SPEAKER_EQ in the host tests): written beside and renamed over, so the plugin
		// never reads half of it; "" removes it. False when it couldn't be written.
		std::string livePath();
		bool apply(const std::string& text);

		// the curve the text makes, in dB, at each of freqs (the filters' shape: the preamp and headroom left out)
		void curve(const std::string& text, const std::vector<double>& freqs, std::vector<double>& db);
	}
}
