#pragma once
// banana-ui: buttons and touch from evdev. Every input device that has buttons or a touchscreen is opened; the
// buttons are mapped to the UI's (Nintendo layout, as the RG DS's joypad driver reports them), and the touchscreen
// on the bottom panel's I2C bus becomes taps and drags on screen 1.
#include <functional>
#include <string>
#include <vector>

namespace banana
{
	enum class Key { NONE, LEFT, RIGHT, UP, DOWN, A, B, X, Y, L, R, START, SELECT, MENU, OTHER };	// (OTHER: back, L3, R3 - the menu's no use; the game panel's by code)

	struct InputEvent
	{
		enum Type { PRESS, RELEASE, TOUCH_DOWN, TOUCH_MOVE, TOUCH_UP } type;
		Key key = Key::NONE;
		int code = 0;			// the evdev code of a button (L2 and L, back: the game panel's Companion button)
		int screen = 1;
		float x = 0, y = 0;		// panel px
		double t = 0;			// when the kernel had it: CLOCK_MONOTONIC ms (BANANA_FRAME_STATS's press-to-panel delay)
	};

	// one stick axis as a d-pad direction: dir held now (-1, 0, 1), v the axis from its centre, half its half range.
	// Pressed past half way out, let go back under a third (a deadzone, and no flicker at the edge); flicked straight
	// to the other side, the other direction
	int stickStep(int dir, int v, int half);
	// a button's evdev code as the menu's key (the face buttons by BANANA_CONFIRM, as a press is read)
	Key inputKey(int code);

	class Input
	{
	public:
		~Input() { close(); }
		// bottomTouch: a fragment of the bottom touchscreen's device path ("fe5e0000.i2c")
		void open(const std::string& bottomTouch, int panelW, int panelH);
		void close();
		// a device came or went in /dev/input (watched with inotify): open() again to take it
		bool rescanDue() { bool r = rescan; rescan = false; return r; }
		int fds(int* out, int max) const;	// the device and watch fds, up to max: no allocation in the frame loop
		// a touchscreen on that screen (0 top, 1 bottom): the RG DS has one on each, the RG DS Plus only the bottom one
		bool hasTouch(int screen) const;
		// read what is waiting on fd; events go to sink
		void read(int fd, const std::function<void(const InputEvent&)>& sink);
		// panels of another size than the canvas or turned (the AYN Thor, panelfit.h): a touch on screen s, as a
		// fraction of its touchscreen's range and whether that range is wider than tall, to the canvas; false in the
		// bars around the picture. Unset: the touchscreen spans the canvas (the RG DS)
		void mapTouch(std::function<bool(int, float, float, bool, float&, float&)> toCanvas) { touchToCanvas = std::move(toCanvas); }
		// the buttons and touchscreens the menu's alone (EVIOCGRAB) while it is up over a stopped game: the game has the
		// same devices open, and what is pressed in the menu would wait there for it. Not a device with the power or a
		// volume key or a switch (the lid): bananad reads those too, and a grab would take them from it. A device with
		// something held (the Home button itself, a stick pushed) is taken once it is let go: the game must get that
		// release, or the button stays down in it. Kept over a rescan (open() again)
		void grab(bool on);

	private:
		struct Dev
		{
			int fd = -1;
			bool shared = false;	// (bananad's too: never grabbed)
			bool ours = false;		// grabbed now
			bool touch = false, pointer = false;	// pointer: an absolute pointer whose button is BTN_LEFT
			int screen = 1;
			int minX = 0, maxX = 0, minY = 0, maxY = 0;
			int x = 0, y = 0;
			bool down = false, wasDown = false, moved = false;
			bool outside = false;	// this touch began in the bars round a fitted picture: none of it is the menu's
			float lastX = 0, lastY = 0;	// (a fitted picture: the touch's last place inside it, in canvas px)
			int hatX = 0, hatY = 0;
			// the left stick as a d-pad (ABS_X/ABS_Y of a pad: the RG DS's retrogame_joypad, -1800..1800 at rest 0):
			// its centre and half range, and which way each axis is held now (-1, 0, 1)
			bool stick = false;
			int stickMid[2] = { 0, 0 }, stickHalf[2] = { 1, 1 }, stickDir[2] = { 0, 0 };
		};
		std::vector<Dev> devs;
		int pw = 640, ph = 480;
		std::function<bool(int, float, float, bool, float&, float&)> touchToCanvas;
		int watch = -1;
		bool rescan = false, grabbed = false;
	};
}
