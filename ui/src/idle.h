#pragma once
// banana-ui: the AMOLED burn-in guard. While the menu runs it owns the panel's backlight (bananad's own idle dim
// leaves this backlight to us while /run/banana/bottom-ui is there): after 90 s with no button or touch it drops to a
// night level, after 180 s it turns the panel off, and the first press or touch restores it and is *swallowed*, so
// waking the screen can never launch a game or change a setting. The backlight paths are passed in, so the headless
// test can point them at files instead of /sys.
#include "sound.h"
#include <fstream>
#include <string>
#include <unistd.h>

namespace banana
{
	class IdleGuard
	{
	public:
		// brightness: the panel's .../brightness file; maximum: its .../max_brightness. Empty: no panel to guard.
		void attach(const std::string& brightness, const std::string& maximum) { bl = brightness; blMax = maximum; ensureOn(); }
		bool attached() const { return !bl.empty(); }
		// the idle thresholds in ms (the test shortens them)
		void setThresholds(int dim, int blank) { dimMs = dim; blankMs = blank; }
		void input(double now) { lastInput = now; }
		int stage() const { return st; }			// 0 awake, 1 dimmed, 2 blanked
		bool engaged() const { return st != 0; }
		int level() const { int c = -1; std::ifstream(bl) >> c; return c; }
		int saved() const { return savedBright; }

		// apply the guard's step for this moment; true when the stage changed
		bool tick(double now, Sound* sound)
		{
			int was = st;
			if (st == 0 && attached() && now - lastInput >= dimMs) dim(sound);
			else if (st == 1 && now - lastInput >= blankMs) blank(sound);
			return st != was;
		}
		// ms until the guard's next step, or -1 when there is none (blanked, or nothing attached)
		int nextMs(double now) const
		{
			if (st == 2 || !attached()) return -1;
			double due = lastInput + (st == 0 ? dimMs : blankMs);
			int left = (int)(due - now) + 1;
			return left < 1 ? 1 : left;
		}
		// the first input while engaged restores the panel and is swallowed: true when it swallowed (and, in every
		// case, the idle timer starts again here)
		bool wake(double now, Sound* sound)
		{
			lastInput = now;
			// The panel can be dark without a dim of ours to undo: the menu was restarted while it was blanked, or a
			// sleep that began with it dark came back at brightness 0. Nothing else would ever turn it on again --
			// the guard believed it was awake -- so a dark panel is always brought back here.
			bool restored = st != 0 ? restorePanel() : ensureOn();
			// The screen is on again: pre-open the sound device, so the *next* press (this one is swallowed) plays
			// its cue instantly instead of waiting for the DSP graph to be instantiated (Sound::setScreenOn).
			if (restored && sound) { sound->setScreenOn(true); sound->setBgmGain(280); }
			return restored;
		}

	private:
		// back from a dim/blank of ours: the level it recorded, and the markers cleared
		bool restorePanel()
		{
			if (savedBright >= 0) write(savedBright);
			unlink("/run/banana/dim");
			unlink("/run/banana/screen-off");
			savedBright = -1;
			st = 0;
			return true;
		}
		// a dark panel with nothing of ours to undo: back to the user's level (bananad keeps it in
		// /data/banana/backlight), else the level the burn-in dim recorded, else 60%. True when it restored one.
		bool ensureOn()
		{
			if (bl.empty()) { st = 0; savedBright = -1; return false; }
			int cur = -1, max = -1;
			{ std::ifstream f(bl); f >> cur; }
			if (cur != 0) { if (st != 0) { st = 0; savedBright = -1; } unlink("/run/banana/screen-off"); return false; }
			{ std::ifstream f(blMax); f >> max; }
			if (max <= 0) return false;
			int v = -1, pm = -1;
			{ std::ifstream f("/data/banana/backlight"); f >> pm; }
			if (pm > 0) v = max * pm / 1000;
			if (v <= 0) { pm = -1; std::ifstream g("/run/banana/dim"); g >> pm; if (pm > 0) v = max * pm / 1000; }
			if (v <= 0) v = max * 60 / 100;
			if (v < 1) v = 1;
			write(v);
			unlink("/run/banana/dim");
			unlink("/run/banana/screen-off");
			savedBright = -1;
			st = 0;
			return true;
		}
		std::string bl, blMax;
		double lastInput = 0;
		int st = 0, savedBright = -1;
		int dimMs = 90000, blankMs = 180000;
		void write(int v) { if (!bl.empty()) { std::ofstream o(bl); o << v << "\n"; } }
		void dim(Sound* sound)
		{
			std::ofstream("/run/banana/screen-off") << "1\n";	// bananad: a power press now wakes the screen, not sleeps
			int cur = -1, max = -1;
			{ std::ifstream f(bl); f >> cur; }
			{ std::ifstream f(blMax); f >> max; }
			if (cur >= 0 && max > 0)
			{
				savedBright = cur;
				int v = max * 5 / 100; if (v < 1) v = 1;	// a 5% floor, still visible
				write(v);
				std::ofstream("/run/banana/dim") << (cur * 1000 / max) << "\n";	// the level before, for Settings
			}
			if (sound) sound->setBgmGain(120);	// the loop a little quieter while dimmed
			st = 1;
		}
		// blanked: the panel off, and the sound device released with it -- the screen being off is the real idle,
		// and the stream lives only while it is on (Sound::setScreenOn)
		void blank(Sound* sound) { std::ofstream("/run/banana/screen-off") << "1\n"; write(0); if (sound) sound->setScreenOn(false); st = 2; }
	};
}
