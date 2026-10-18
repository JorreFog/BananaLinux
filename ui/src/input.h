#pragma once
// banana-ui: buttons and touch from evdev. Every input device that has buttons or a touchscreen is opened; the
// buttons are mapped to the UI's (Nintendo layout, as the RG DS's joypad driver reports them), and the touchscreen
// on the bottom panel's I2C bus becomes taps and drags on screen 1.
#include <functional>
#include <string>
#include <vector>

namespace banana
{
	enum class Key { NONE, LEFT, RIGHT, UP, DOWN, A, B, X, Y, L, R, START, SELECT, MENU };

	struct InputEvent
	{
		enum Type { PRESS, RELEASE, TOUCH_DOWN, TOUCH_MOVE, TOUCH_UP } type;
		Key key = Key::NONE;
		int screen = 1;
		float x = 0, y = 0;		// panel px
	};

	class Input
	{
	public:
		~Input() { close(); }
		// bottomTouch: a fragment of the bottom touchscreen's device path ("fe5e0000.i2c")
		void open(const std::string& bottomTouch, int panelW, int panelH);
		void close();
		// a device came or went in /dev/input (watched with inotify): open() again to take it
		bool rescanDue() { bool r = rescan; rescan = false; return r; }
		std::vector<int> fds() const;
		// a touchscreen on that screen (0 top, 1 bottom): the RG DS has one on each, the RG DS Plus only the bottom one
		bool hasTouch(int screen) const;
		// read what is waiting on fd; events go to sink
		void read(int fd, const std::function<void(const InputEvent&)>& sink);

	private:
		struct Dev
		{
			int fd = -1;
			bool touch = false, pointer = false;	// pointer: an absolute pointer whose button is BTN_LEFT
			int screen = 1;
			int minX = 0, maxX = 0, minY = 0, maxY = 0;
			int x = 0, y = 0;
			bool down = false, wasDown = false, moved = false;
			int hatX = 0, hatY = 0;
		};
		std::vector<Dev> devs;
		int pw = 640, ph = 480;
		int watch = -1;
		bool rescan = false;
	};
}
