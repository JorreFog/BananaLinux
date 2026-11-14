// banana-ui on Android: the Input (input.h) fed by the launcher's activities instead of evdev. Built in place of
// input.cpp. The app turns Android's KeyEvents into the kernel's key codes (the pad's own scan code where it has one,
// so the buttons mean what they mean on BananaOS's own image, BANANA_CONFIRM included), its joystick MotionEvents into
// stick and hat positions, and its touches into fractions of the touched surface; they wait in a queue here, and the
// core's loop reads them through one eventfd (fds()), as it reads evdev devices on Linux.
#ifdef __ANDROID__
#include "input.h"
#include "android_host.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <linux/input.h>
#include <map>
#include <mutex>
#include <sys/eventfd.h>
#include <unistd.h>

namespace banana
{
	// (as input.cpp: the stick's deadzone and hysteresis, the face buttons by BANANA_CONFIRM, a trigger's rest)
	int stickStep(int dir, int v, int half)
	{
		if (dir == 0 && std::abs(v) * 2 > half) return v < 0 ? -1 : 1;
		if (dir != 0 && (std::abs(v) * 3 < half || (v < 0) != (dir < 0))) return std::abs(v) * 2 > half ? (v < 0 ? -1 : 1) : 0;
		return dir;
	}

	static bool southConfirm()
	{
		const char* e = getenv("BANANA_CONFIRM");
		return e && !strcmp(e, "south");
	}

	Key inputKey(int code)
	{
		if (southConfirm())
			switch (code)
			{
			case BTN_SOUTH: return Key::A;
			case BTN_EAST: return Key::B;
			case BTN_WEST: return Key::X;
			case BTN_NORTH: return Key::Y;
			default: break;
			}
		switch (code)
		{
		case BTN_DPAD_LEFT: case KEY_LEFT: return Key::LEFT;
		case BTN_DPAD_RIGHT: case KEY_RIGHT: return Key::RIGHT;
		case BTN_DPAD_UP: case KEY_UP: return Key::UP;
		case BTN_DPAD_DOWN: case KEY_DOWN: return Key::DOWN;
		case BTN_EAST: case KEY_ENTER: return Key::A;
		case BTN_SOUTH: case KEY_BACKSPACE: case KEY_ESC: return Key::B;
		case BTN_NORTH: case KEY_X: return Key::X;
		case BTN_WEST: case KEY_Y: return Key::Y;
		case BTN_TL: case BTN_TL2: case KEY_PAGEUP: return Key::L;
		case BTN_TR: case BTN_TR2: case KEY_PAGEDOWN: return Key::R;
		case BTN_START: case KEY_S: return Key::START;
		case BTN_SELECT: case KEY_TAB: return Key::SELECT;
		case BTN_MODE: case KEY_F1: return Key::MENU;
		case BTN_Z: case BTN_THUMBL: case BTN_THUMBR: case BTN_BACK: return Key::OTHER;
		default: return Key::NONE;
		}
	}

	bool absPushed(int axis, bool rightStick, int lo, int hi, int value)
	{
		if (hi <= lo) return false;
		const bool trigger = (axis == ABS_Z || axis == ABS_RZ) && rightStick && lo >= 0;
		const long range = (long)hi - lo;
		const long out = trigger ? 2 * ((long)value - lo) : labs(2L * value - lo - hi);
		return 2 * out > range;
	}

	namespace
	{
		struct Raw
		{
			enum { KEY, AXES, TOUCH } kind;
			bool down = false;
			int code = 0, device = 0, screen = 1, action = 0;
			float x = 0, y = 0, hx = 0, hy = 0;
			bool wide = true;
			double t = 0;
		};
		std::mutex qm;
		std::deque<Raw> queue;
		int qfd = -1;
		int queueFd()
		{
			static std::once_flag once;
			std::call_once(once, [] { qfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC); });
			return qfd;
		}
		void push(const Raw& r)
		{
			{
				std::lock_guard<std::mutex> l(qm);
				if (queue.size() > 512) queue.pop_front();	// (a core that isn't reading: the oldest go)
				queue.push_back(r);
			}
			android::wake();
		}
		// per pad: which way its left stick and its hat are held now (-1, 0, 1)
		struct Pad { int stick[2] = { 0, 0 }, hat[2] = { 0, 0 }; };
		std::map<int, Pad> pads;
		// per screen: the touch's state, as input.cpp keeps it per touchscreen
		struct Finger { bool down = false, outside = false; float lastX = 0, lastY = 0; };
		Finger fingers[2];
	}

	namespace android
	{
		void wake()
		{
			uint64_t one = 1;
			if (queueFd() >= 0 && write(queueFd(), &one, 8) < 0) {}
		}
		void pushKey(bool down, int code, double t) { Raw r{ Raw::KEY }; r.down = down; r.code = code; r.t = t; push(r); }
		void pushAxes(int device, float x, float y, float hx, float hy, double t)
		{
			Raw r{ Raw::AXES }; r.device = device; r.x = x; r.y = y; r.hx = hx; r.hy = hy; r.t = t; push(r);
		}
		void pushTouch(int s, int action, float nx, float ny, bool wide, double t)
		{
			Raw r{ Raw::TOUCH }; r.screen = s; r.action = action; r.x = nx; r.y = ny; r.wide = wide; r.t = t; push(r);
		}
	}

	void Input::open(const std::string&, int panelW, int panelH)
	{
		pw = panelW; ph = panelH;
		watch = queueFd();
	}

	void Input::close() { watch = -1; }

	int Input::fds(int* out, int max) const
	{
		if (watch < 0 || max < 1) return 0;
		out[0] = watch;
		return 1;
	}

	// both screens are touchscreens on the AYN Thor (and a phone's one screen is the top)
	bool Input::hasTouch(int) const { return true; }

	// (no grab on Android: the window in focus gets the keys; the quick sheet's overlay takes the focus while it is up)
	void Input::grab(bool on) { grabbed = on; }

	void Input::read(int fd, const std::function<void(const InputEvent&)>& sink)
	{
		if (fd != watch || fd < 0) return;
		uint64_t v;
		if (::read(fd, &v, 8) < 0) {}
		std::deque<Raw> got;
		{
			std::lock_guard<std::mutex> l(qm);
			got.swap(queue);
		}
		for (const Raw& r : got)
		{
			if (r.kind == Raw::KEY)
			{
				InputEvent o;
				o.type = r.down ? InputEvent::PRESS : InputEvent::RELEASE;
				o.code = r.code;
				o.key = inputKey(r.code);
				o.t = r.t;
				// (a key the menu has no use for still goes out with its code: the Home button's is the core's to know)
				if (o.key == Key::NONE) o.key = Key::OTHER;
				sink(o);
			}
			else if (r.kind == Raw::AXES)
			{
				Pad& pd = pads[r.device];
				const float ax[2] = { r.x, r.y };
				for (int a = 0; a < 2; a++)
				{
					int dir = stickStep(pd.stick[a], (int)std::lround(ax[a] * 1000), 1000);
					if (dir == pd.stick[a]) continue;
					Key neg = a ? Key::UP : Key::LEFT, pos = a ? Key::DOWN : Key::RIGHT;
					if (pd.stick[a] != 0) { InputEvent o; o.type = InputEvent::RELEASE; o.key = pd.stick[a] < 0 ? neg : pos; o.t = r.t; sink(o); }
					if (dir != 0) { InputEvent o; o.type = InputEvent::PRESS; o.key = dir < 0 ? neg : pos; o.t = r.t; sink(o); }
					pd.stick[a] = dir;
				}
				const float hv[2] = { r.hx, r.hy };
				for (int a = 0; a < 2; a++)
				{
					int h = hv[a] < -0.5f ? -1 : hv[a] > 0.5f ? 1 : 0;
					if (h == pd.hat[a]) continue;
					Key neg = a ? Key::UP : Key::LEFT, pos = a ? Key::DOWN : Key::RIGHT;
					int negCode = a ? BTN_DPAD_UP : BTN_DPAD_LEFT, posCode = a ? BTN_DPAD_DOWN : BTN_DPAD_RIGHT;
					if (pd.hat[a] != 0) { InputEvent o; o.type = InputEvent::RELEASE; o.key = pd.hat[a] < 0 ? neg : pos; o.code = pd.hat[a] < 0 ? negCode : posCode; o.t = r.t; sink(o); }
					if (h != 0) { InputEvent o; o.type = InputEvent::PRESS; o.key = h < 0 ? neg : pos; o.code = h < 0 ? negCode : posCode; o.t = r.t; sink(o); }
					pd.hat[a] = h;
				}
			}
			else
			{
				const int s = r.screen == 0 ? 0 : 1;
				Finger& f = fingers[s];
				InputEvent o;
				o.screen = s;
				o.t = r.t;
				o.x = r.x * pw; o.y = r.y * ph;
				const bool down = r.action != 2;
				if (touchToCanvas)
				{
					// the surface shows the canvas fitted (panelfit.h): the bars round it aren't the menu's
					bool in = touchToCanvas(s, r.x, r.y, r.wide, o.x, o.y);
					if (r.action == 0) f.outside = !in;
					if (f.outside) { if (!down) f.outside = false; f.down = down; continue; }
					if (in) { f.lastX = o.x; f.lastY = o.y; }
					else { o.x = f.lastX; o.y = f.lastY; if (down && r.action == 1) continue; }
				}
				if (r.action == 0) o.type = InputEvent::TOUCH_DOWN;
				else if (r.action == 1) { if (!f.down) continue; o.type = InputEvent::TOUCH_MOVE; }
				else { if (!f.down) continue; o.type = InputEvent::TOUCH_UP; }
				f.down = down;
				sink(o);
			}
		}
	}
}
#endif
