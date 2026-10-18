#include "input.h"
#include <climits>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace banana
{
	static bool testBit(const unsigned long* bits, int n)
	{
		return (bits[n / (8 * sizeof(long))] >> (n % (8 * sizeof(long)))) & 1;
	}

	static Key mapKey(int code)
	{
		switch (code)
		{
		case BTN_DPAD_LEFT: case KEY_LEFT: return Key::LEFT;
		case BTN_DPAD_RIGHT: case KEY_RIGHT: return Key::RIGHT;
		case BTN_DPAD_UP: case KEY_UP: return Key::UP;
		case BTN_DPAD_DOWN: case KEY_DOWN: return Key::DOWN;
		case BTN_EAST: case KEY_ENTER: return Key::A;		// the RG DS's A is east (Nintendo layout)
		case BTN_SOUTH: case KEY_BACKSPACE: case KEY_ESC: return Key::B;
		case BTN_NORTH: case KEY_X: return Key::X;
		case BTN_WEST: case KEY_Y: return Key::Y;
		case BTN_TL: case BTN_TL2: case KEY_PAGEUP: return Key::L;
		case BTN_TR: case BTN_TR2: case KEY_PAGEDOWN: return Key::R;
		case BTN_START: case KEY_S: return Key::START;
		case BTN_SELECT: case KEY_TAB: return Key::SELECT;
		case BTN_MODE: case KEY_F1: return Key::MENU;
		default: return Key::NONE;
		}
	}

	// the device's path in sysfs (to tell the bottom panel's touchscreen from the top one)
	static std::string sysPath(const char* node)
	{
		char link[512], real[PATH_MAX];
		snprintf(link, sizeof link, "/sys/class/input/%s/device", node);
		if (!realpath(link, real)) return "";
		return real;
	}

	void Input::open(const std::string& bottomTouch, int panelW, int panelH)
	{
		close();
		pw = panelW; ph = panelH;
		watch = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
		if (watch >= 0) inotify_add_watch(watch, "/dev/input", IN_CREATE | IN_DELETE | IN_ATTRIB);
		DIR* d = opendir("/dev/input");
		if (!d) return;
		while (dirent* e = readdir(d))
		{
			if (strncmp(e->d_name, "event", 5)) continue;
			std::string path = std::string("/dev/input/") + e->d_name;
			int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
			if (fd < 0) continue;
			unsigned long keys[KEY_MAX / (8 * sizeof(long)) + 1] = {}, abs[ABS_MAX / (8 * sizeof(long)) + 1] = {};
			ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys);
			ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs), abs);
			Dev dv;
			dv.fd = fd;
			// a touchscreen (multitouch, or single-touch with BTN_TOUCH), or an absolute pointer (QEMU's tablet, a
			// graphics tablet: BTN_LEFT on ABS_X/Y, and not a pad's stick)
			bool mt = testBit(abs, ABS_MT_POSITION_X) && testBit(abs, ABS_MT_POSITION_Y);
			bool st = !mt && testBit(abs, ABS_X) && testBit(abs, ABS_Y) && (testBit(keys, BTN_TOUCH) || (testBit(keys, BTN_LEFT) && !testBit(keys, BTN_SOUTH)));
			if (mt || st)
			{
				dv.touch = true;
				dv.pointer = st && !testBit(keys, BTN_TOUCH);
				input_absinfo ax = {}, ay = {};
				ioctl(fd, EVIOCGABS(mt ? ABS_MT_POSITION_X : ABS_X), &ax);
				ioctl(fd, EVIOCGABS(mt ? ABS_MT_POSITION_Y : ABS_Y), &ay);
				dv.minX = ax.minimum; dv.maxX = ax.maximum; dv.minY = ay.minimum; dv.maxY = ay.maximum;
				// where the finger last was: a touch at the same spot again sends no position (the kernel drops a value
				// that didn't change), and the devices are opened again after every game. ABS_X/Y hold it (a multitouch
				// screen's pointer emulation); the ABS_MT_POSITION axes read 0 here (the kernel keeps them per slot)
				input_absinfo px = {}, py = {};
				if (testBit(abs, ABS_X) && testBit(abs, ABS_Y) && ioctl(fd, EVIOCGABS(ABS_X), &px) == 0 && ioctl(fd, EVIOCGABS(ABS_Y), &py) == 0)
				{ dv.x = px.value; dv.y = py.value; }
				else { dv.x = ax.value; dv.y = ay.value; }
				dv.screen = sysPath(e->d_name).find(bottomTouch) != std::string::npos ? 1 : 0;
			}
			else
			{
				bool any = testBit(abs, ABS_HAT0X);
				for (int k = KEY_ESC; k < KEY_MAX && !any; k++)
					if (testBit(keys, k) && mapKey(k) != Key::NONE) any = true;
				if (!any) { ::close(fd); continue; }
			}
			devs.push_back(dv);
		}
		closedir(d);
	}

	void Input::close()
	{
		for (auto& d : devs) ::close(d.fd);
		devs.clear();
		if (watch >= 0) ::close(watch);
		watch = -1;
	}

	std::vector<int> Input::fds() const
	{
		std::vector<int> r;
		for (auto& d : devs) r.push_back(d.fd);
		if (watch >= 0) r.push_back(watch);
		return r;
	}

	bool Input::hasTouch(int screen) const
	{
		for (auto& d : devs) if (d.touch && d.screen == screen) return true;
		return false;
	}

	void Input::read(int fd, const std::function<void(const InputEvent&)>& sink)
	{
		if (fd == watch)
		{
			char buf[4096];
			while (::read(fd, buf, sizeof buf) > 0) {}
			rescan = true;
			return;
		}
		Dev* dv = nullptr;
		for (auto& d : devs) if (d.fd == fd) dv = &d;
		if (!dv) return;
		// a device that went away: its next read fails, and the rescan drops it
		input_event ev[64];
		ssize_t n;
		while ((n = ::read(fd, ev, sizeof ev)) > 0)
		{
			for (int i = 0; i < n / (int)sizeof(input_event); i++)
			{
				const input_event& e = ev[i];
				if (dv->touch)
				{
					if (e.type == EV_ABS && (e.code == ABS_MT_POSITION_X || e.code == ABS_X)) { dv->x = e.value; dv->moved = true; }
					else if (e.type == EV_ABS && (e.code == ABS_MT_POSITION_Y || e.code == ABS_Y)) { dv->y = e.value; dv->moved = true; }
					else if (e.type == EV_ABS && e.code == ABS_MT_TRACKING_ID && e.value < 0) dv->down = false;
					else if (e.type == EV_KEY && (e.code == BTN_TOUCH || (dv->pointer && e.code == BTN_LEFT))) dv->down = e.value != 0;
					else if (e.type == EV_SYN && e.code == SYN_REPORT)
					{
						InputEvent o;
						o.screen = dv->screen;
						float sx = dv->maxX > dv->minX ? (float)pw / (dv->maxX - dv->minX + 1) : 1;
						float sy = dv->maxY > dv->minY ? (float)ph / (dv->maxY - dv->minY + 1) : 1;
						o.x = (dv->x - dv->minX) * sx; o.y = (dv->y - dv->minY) * sy;
						if (dv->down && !dv->wasDown) { o.type = InputEvent::TOUCH_DOWN; sink(o); }
						else if (dv->down && dv->moved) { o.type = InputEvent::TOUCH_MOVE; sink(o); }
						else if (!dv->down && dv->wasDown) { o.type = InputEvent::TOUCH_UP; sink(o); }
						dv->wasDown = dv->down;
						dv->moved = false;
					}
					continue;
				}
				if (e.type == EV_KEY && e.value != 2)
				{
					Key k = mapKey(e.code);
					if (k == Key::NONE) continue;
					InputEvent o;
					o.type = e.value ? InputEvent::PRESS : InputEvent::RELEASE;
					o.key = k;
					sink(o);
				}
				else if (e.type == EV_ABS && (e.code == ABS_HAT0X || e.code == ABS_HAT0Y))
				{
					int& last = e.code == ABS_HAT0X ? dv->hatX : dv->hatY;
					Key neg = e.code == ABS_HAT0X ? Key::LEFT : Key::UP, pos = e.code == ABS_HAT0X ? Key::RIGHT : Key::DOWN;
					if (last != 0) { InputEvent o; o.type = InputEvent::RELEASE; o.key = last < 0 ? neg : pos; sink(o); }
					if (e.value != 0) { InputEvent o; o.type = InputEvent::PRESS; o.key = e.value < 0 ? neg : pos; sink(o); }
					last = e.value;
				}
			}
		}
	}
}
