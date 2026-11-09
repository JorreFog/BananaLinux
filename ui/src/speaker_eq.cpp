// The speaker EQ's settings and the profile the ALSA plugin plays (speaker_eq.h).
#include "speaker_eq.h"

#include "eq/eq.h"
#include "settings.h"
#include "settings_defaults.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace banana
{
	namespace speakereq
	{
		const int BAND_HZ[BANDS] = { 250, 500, 1000, 2000, 4000, 8000, 16000 };
		static const double BAND_Q = 1.41;	// an octave wide: next to each other they make a smooth curve, not ripples
		static const double RATE = 48000;	// (the speakers' stream: bananad pins it)

		static std::string board()
		{
			if (const char* b = getenv("BANANA_BOARD")) return b;
			std::ifstream f("/etc/banana/board");
			std::string s;
			std::getline(f, s);
			while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\t')) s.pop_back();
			return s;
		}

		// a card file's name as Profile can name it: no folders, no hidden files, .txt
		static bool cardName(const std::string& n)
		{
			if (n.size() < 5 || n.size() > 80 || n[0] == '.' || n.compare(n.size() - 4, 4, ".txt")) return false;
			return n.find('/') == std::string::npos && n.find('\\') == std::string::npos;
		}

		static std::string readText(const std::string& path)
		{
			std::ifstream f(path, std::ios::binary);
			if (!f) return "";
			std::string s(64 * 1024, '\0');	// (a profile is a few hundred bytes; more is not one)
			f.read(&s[0], (std::streamsize)s.size());
			s.resize((size_t)f.gcount());
			return s;
		}

		State load(const std::string& dataDir)
		{
			const std::string conf = dataDir + "/settings.conf";
			State s;
			s.on = getConfValue(conf, "speaker_eq", settingDefault("speaker_eq")) != "off";
			s.profile = getConfValue(conf, "speaker_eq_profile", settingDefault("speaker_eq_profile"));
			if (s.profile != "tuned" && s.profile != "none" && !cardName(s.profile)) s.profile = "tuned";
			std::stringstream bands(getConfValue(conf, "speaker_eq_bands", settingDefault("speaker_eq_bands")));
			std::string v;
			for (int i = 0; i < BANDS && std::getline(bands, v, ','); i++)
				s.gain[i] = std::max(-BAND_MAX_DB, std::min(BAND_MAX_DB, atoi(v.c_str())));
			return s;
		}

		void save(const std::string& dataDir, const State& s)
		{
			const std::string conf = dataDir + "/settings.conf";
			std::string bands;
			for (int i = 0; i < BANDS; i++) bands += (i ? "," : "") + std::to_string(s.gain[i]);
			setConfValue(conf, "speaker_eq", s.on ? "on" : "off");
			setConfValue(conf, "speaker_eq_profile", s.profile);
			setConfValue(conf, "speaker_eq_bands", bands);
		}

		// the tuned profile's name: the handheld's ("RG DS Plus Tuned"), so it reads as this speaker's correction, not a style
		static std::string tunedName()
		{
			const std::string b = board();
			return (b == "rgds-plus" ? "RG DS Plus" : b == "rgds" ? "RG DS" : "Handheld") + std::string(" Tuned");
		}

		static std::string tunedPath(const std::string& shareDir)
		{
			std::string b = board();
			return b.empty() || b.find('/') != std::string::npos ? "" : shareDir + "/" + b + ".txt";
		}

		std::string shareDir(const std::string& assets) { return assets + "/../speaker-eq"; }

		std::string profileName(const std::string& value, const std::string& dataDir, const std::string& shareDir)
		{
			if (value == "tuned")
			{
				const std::string t = tunedPath(shareDir);
				return !t.empty() && access(t.c_str(), R_OK) == 0 ? tunedName() : "None";	// (a board with none: nothing plays)
			}
			if (value == "none" || !cardName(value)) return "None";
			const std::string n = value.substr(0, value.size() - 4);
			return access((dataDir + "/speaker-eq/" + value).c_str(), R_OK) == 0 ? n : n + " (not on the card)";
		}

		bool adjusted(const State& s)
		{
			for (int g : s.gain) if (g) return true;
			return false;
		}

		std::vector<Choice> profiles(const std::string& dataDir, const std::string& shareDir, const std::string& current)
		{
			std::vector<Choice> c;
			std::string tuned = tunedPath(shareDir);
			if (!tuned.empty() && access(tuned.c_str(), R_OK) == 0) c.push_back({ "tuned", tunedName() });
			c.push_back({ "none", "None" });
			std::vector<std::string> files;
			if (DIR* d = opendir((dataDir + "/speaker-eq").c_str()))
			{
				while (dirent* e = readdir(d)) if (cardName(e->d_name)) files.push_back(e->d_name);
				closedir(d);
			}
			std::sort(files.begin(), files.end());
			bool found = current == "tuned" || current == "none";
			for (auto& f : files)
			{
				c.push_back({ f, f.substr(0, f.size() - 4) });
				if (f == current) found = true;
			}
			if (!found && cardName(current)) c.push_back({ current, current.substr(0, current.size() - 4) + " (not on the card)" });
			return c;
		}

		std::string profileText(const State& s, const std::string& dataDir, const std::string& shareDir)
		{
			if (s.profile == "tuned") { std::string t = tunedPath(shareDir); return t.empty() ? "" : readText(t); }
			if (s.profile != "none" && cardName(s.profile)) return readText(dataDir + "/speaker-eq/" + s.profile);
			return "";
		}

		std::string compose(const State& s, const std::string& dataDir, const std::string& shareDir)
		{
			return s.on ? compose(s, profileText(s, dataDir, shareDir)) : "";
		}

		bool applySaved(const std::string& dataDir, const std::string& shareDir)
		{
			return apply(compose(load(dataDir), dataDir, shareDir));
		}

		std::string compose(const State& s, const std::string& text)
		{
			if (!s.on) return "";
			struct eq_profile p;
			eq_parse(text.data(), text.size(), &p);
			// the sliders after the profile's own filters (a profile with more than room leaves room for them)
			int sliders = 0;
			for (int i = 0; i < BANDS; i++) if (s.gain[i]) sliders++;
			p.n = std::min(p.n, EQ_MAX_BANDS - sliders);
			for (int i = 0; i < BANDS; i++)
				if (s.gain[i]) p.b[p.n++] = { EQ_PK, (double)BAND_HZ[i], (double)s.gain[i], BAND_Q };
			if (!p.n && p.preamp == 0) return "";
			static const char* const TYPE[] = { "PK", "LSC", "HSC", "LPQ", "HPQ" };
			char ln[160];
			snprintf(ln, sizeof ln, "# banana-ui: Settings > Display & sound > Speaker EQ (%s)\nPreamp: %.2f dB\n",
				s.profile == "tuned" ? tunedName().c_str() : s.profile == "none" ? "no profile" : s.profile.c_str(), p.preamp);
			std::string out = ln;
			for (int i = 0; i < p.n; i++)
			{
				const eq_band& b = p.b[i];
				if (b.type == EQ_LP || b.type == EQ_HP)
					snprintf(ln, sizeof ln, "Filter %d: ON %s Fc %.2f Hz Q %.4f\n", i + 1, TYPE[b.type], b.f, b.q);
				else
					snprintf(ln, sizeof ln, "Filter %d: ON %s Fc %.2f Hz Gain %.2f dB Q %.4f\n", i + 1, TYPE[b.type], b.f, b.gain, b.q);
				out += ln;
			}
			return out;
		}

		std::string livePath()
		{
			const char* p = getenv("BANANA_SPEAKER_EQ");
			return p && *p ? p : "/run/banana/speaker-eq";
		}

		bool apply(const std::string& text)
		{
			const std::string path = livePath(), tmp = path + ".new";
			if (text.empty()) return unlink(path.c_str()) == 0 || access(path.c_str(), F_OK) != 0;
			FILE* f = fopen(tmp.c_str(), "w");
			if (!f) return false;
			bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
			ok = fclose(f) == 0 && ok;
			if (!ok || rename(tmp.c_str(), path.c_str()) != 0) { unlink(tmp.c_str()); return false; }
			return true;
		}

		void curve(const std::string& text, const std::vector<double>& freqs, std::vector<double>& db)
		{
			struct eq_profile p;
			struct eq_chain ch;
			eq_parse(text.data(), text.size(), &p);
			eq_design(&p, RATE, &ch);
			db.resize(freqs.size());
			for (size_t i = 0; i < freqs.size(); i++) db[i] = ch.n ? eq_response(&ch, freqs[i], RATE) : 0;
		}
	}
}
