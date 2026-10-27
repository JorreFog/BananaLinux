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
		void attach(const std::string& brightness, const std::string& maximum) { bl = brightness; blMax = maximum; }
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
			if (st == 0) return false;
			if (savedBright >= 0) write(savedBright);
			unlink("/run/banana/dim");
			if (sound) sound->setBgmGain(280);
			savedBright = -1;
			st = 0;
			return true;
		}

	private:
		std::string bl, blMax;
		double lastInput = 0;
		int st = 0, savedBright = -1;
		int dimMs = 90000, blankMs = 180000;
		void write(int v) { if (!bl.empty()) { std::ofstream o(bl); o << v << "\n"; } }
		void dim(Sound* sound)
		{
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
		void blank(Sound* sound) { write(0); if (sound) sound->setBgmGain(0); st = 2; }
	};
}
