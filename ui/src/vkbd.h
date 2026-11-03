#pragma once

namespace banana
{
	// A keyboard the panel can type with: a virtual one on /dev/uinput, so the emulator reads it exactly as a USB
	// keyboard (RetroArch's udev driver, BananaFront's evdev input) and the game's core gets its keys, whether it
	// asks for them through RETRO_DEVICE_KEYBOARD (Fuse) or a keyboard callback (VICE). Made for the game panel's
	// on-screen keyboard (a C64's or ZX Spectrum's LOAD"*",8,1 typed with a finger on the bottom screen).
	class VirtualKeyboard
	{
	public:
		~VirtualKeyboard() { close(); }
		// the device for this game (its name on the emulator's side); false: no /dev/uinput or no permission
		bool open(const char* name = "Banana on-screen keyboard");
		bool ready() const { return fd >= 0; }
		// one key, by its Linux key code (KEY_A, KEY_1, KEY_ENTER...); shift holds the left shift around it
		void tap(int code, bool shift = false);
		void close();
	private:
		void emit(int code, int value);
		void sync();
		int fd = -1;
	};
}
