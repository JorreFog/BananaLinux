#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace banana
{
	// A keyboard the panel can type with: a virtual one on /dev/uinput, so the emulator reads it exactly as a USB
	// keyboard (RetroArch's udev driver, BananaFront's evdev input) and the game's core gets its keys, whether it
	// asks for them through RETRO_DEVICE_KEYBOARD (Fuse) or a keyboard callback (VICE). Made for the game panel's
	// on-screen keyboard (a C64's or ZX Spectrum's LOAD"*",8,1 typed with a finger on the bottom screen).
	//
	// A key is held for KEY_HOLD_MS before its release: a core that samples its keyboard once a frame (VICE's
	// callback only keeps the state until process_key runs; RETRO_DEVICE_KEYBOARD is read at the frame too) never
	// saw a press and release that came in between two frames, and an on-screen tap is exactly that. A thread of
	// its own sends the releases, so the panel never waits for them.
	class VirtualKeyboard
	{
	public:
		~VirtualKeyboard() { close(); }
		// the device for this game (its name on the emulator's side); false: no /dev/uinput or no permission
		bool open(const char* name = "Banana on-screen keyboard");
		bool ready() const { return fd >= 0; }
		// one key, by its Linux key code (KEY_A, KEY_1, KEY_ENTER...); shift holds the left shift around it. The
		// press goes out at once, the release KEY_HOLD_MS later (on the keyboard's own thread)
		void tap(int code, bool shift = false);
		void close();
		static const int KEY_HOLD_MS = 50;	// (three frames at 60 Hz, two and a half at 50: every core sees it)
	private:
		struct Pending { int code; bool shift; };
		void emit(int code, int value);
		void sync();
		void worker();
		int fd = -1;
		std::thread th;
		std::mutex mu;
		std::condition_variable cv;
		std::deque<std::pair<Pending, std::chrono::steady_clock::time_point>> q;	// the keys held, and when each is released
		bool stop = false;
	};
}
