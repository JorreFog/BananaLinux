// vkbd.cpp: the game panel's on-screen keyboard, as a virtual keyboard on /dev/uinput. The emulator sees an ordinary
// USB keyboard named "Banana on-screen keyboard", so no command has to go through its pipe and both frontends hear it
// the same way (RetroArch's udev input, BananaFront's evdev input); the core then gets RETRO_DEVICE_KEYBOARD as from a
// keyboard plugged in. The device exists only while the game runs (created before it starts, gone after it ends).
#include "vkbd.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace banana
{
	namespace
	{
		// every key the panel's keyboard sends (its three pages: letters and digits, symbols, and the keys a game
		// uses: Return, Space, Delete, Escape, the arrows, F1-F12, Ctrl, Tab, Caps Lock)
		const int KEYS[] = {
			KEY_LEFTSHIFT, KEY_LEFTCTRL, KEY_ENTER, KEY_BACKSPACE, KEY_ESC, KEY_SPACE, KEY_TAB, KEY_CAPSLOCK,
			KEY_HOME, KEY_END, KEY_INSERT, KEY_DELETE, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_PAGEUP, KEY_PAGEDOWN,
			KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
			KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
			KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
			KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
			KEY_MINUS, KEY_EQUAL, KEY_LEFTBRACE, KEY_RIGHTBRACE, KEY_SEMICOLON, KEY_APOSTROPHE, KEY_GRAVE,
			KEY_BACKSLASH, KEY_COMMA, KEY_DOT, KEY_SLASH,
		};
	}

	bool VirtualKeyboard::open(const char* name)
	{
		close();
		fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0) { fd = ::open("/dev/input/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC); if (fd < 0) return false; }
		ioctl(fd, UI_SET_EVBIT, EV_KEY);
		ioctl(fd, UI_SET_EVBIT, EV_SYN);
		for (int k : KEYS) ioctl(fd, UI_SET_KEYBIT, k);
		uinput_setup us{};
		us.id.bustype = BUS_VIRTUAL;
		us.id.vendor = 0x1209;		// (pid.codes' vendor id, for a device of our own)
		us.id.product = 0x0001;
		us.id.version = 1;
		snprintf(us.name, sizeof us.name, "%s", name);
		if (ioctl(fd, UI_DEV_SETUP, &us) < 0 || ioctl(fd, UI_DEV_CREATE) < 0)
		{
			int e = errno;
			::close(fd);
			fd = -1;
			errno = e;
			return false;
		}
		usleep(60000);	// (the kernel's udev node and the emulator's inotify need it to be there before the first key)
		return true;
	}

	void VirtualKeyboard::emit(int code, int value)
	{
		if (fd < 0) return;
		input_event ev{};
		ev.type = EV_KEY;
		ev.code = (unsigned short)code;
		ev.value = value;
		if (write(fd, &ev, sizeof ev) < 0) {}
	}

	void VirtualKeyboard::sync()
	{
		if (fd < 0) return;
		input_event ev{};
		ev.type = EV_SYN;
		ev.code = SYN_REPORT;
		if (write(fd, &ev, sizeof ev) < 0) {}
	}

	void VirtualKeyboard::tap(int code, bool shift)
	{
		if (fd < 0 || code == 0) return;
		if (shift) { emit(KEY_LEFTSHIFT, 1); sync(); }
		emit(code, 1);
		sync();
		emit(code, 0);
		sync();
		if (shift) { emit(KEY_LEFTSHIFT, 0); sync(); }
	}

	void VirtualKeyboard::close()
	{
		if (fd < 0) return;
		ioctl(fd, UI_DEV_DESTROY);
		::close(fd);
		fd = -1;
	}
}
