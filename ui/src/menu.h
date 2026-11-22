#pragma once
// banana-ui: the menu of the theme in use, as the main loop drives it. Dark and Light are ROCKNIXDS Pixel's engine
// (rnds::UI, used as it is through RndsMenu); Carts is Banana's own (carts.cpp). The calls are rnds::UI's.
#include "../rnds/RndsUI.h"
#include <cmath>
#include <functional>
#include <string>

namespace banana
{
	class Menu
	{
	public:
		using View = rnds::UI::View;
		using Button = rnds::UI::Button;
		virtual ~Menu() {}
		virtual void setHome(int sys) = 0;
		virtual void setLibrary(int sys, int game) = 0;
		virtual void dataChanged() = 0;
		virtual View view() const = 0;
		virtual int systemIndex() const = 0;
		virtual int gameIndex() const = 0;
		virtual bool press(Button b) = 0;
		virtual bool touch(int screen, float x, float y, bool down) = 0;
		virtual bool touchMove(int screen, float x, float y) = 0;
		virtual void touchCancel() = 0;
		virtual void update(double nowMs) = 0;
		virtual void render() = 0;
		virtual int nextWake() const = 0;
		virtual bool workOnce() = 0;
		virtual void finishWork() = 0;
		virtual void setNotifiers(std::function<void()> queued, std::function<void()> finished) = 0;
		virtual bool animating() const = 0;
		virtual void setLog(std::function<void(const std::string&)> log) = 0;
		// where Parental controls' play time left goes on the top screen, its pill's centre (in the 640x480 layout), and
		// the room it has (a narrower one shortens it): the middle of the status bar, or a theme's own spot (a layout
		// theme's "timeLeft": { "x", "y" })
		virtual void timeLeftAt(float& x, float& y, float& room) const { x = 320; y = 32; room = 640; }
		virtual void trim() {}	// a game is starting: drawn pictures it can make again let go (the memory is the game's)
		// workOnce guards what it shares with the drawing itself: the worker may run it without the UI's lock
		virtual bool ownLocking() const { return false; }
	};

	// ROCKNIXDS Pixel's engine, the Dark and Light themes
	class RndsMenu : public Menu
	{
	public:
		RndsMenu(rnds::Source* src, rnds::Backend* be, const std::string& assets, float scale, const std::string& variant)
			: ui(src, be, assets, scale, variant) {}
		void setHome(int sys) override { ui.setHome(sys); }
		void setLibrary(int sys, int game) override { ui.setLibrary(sys, game); }
		void dataChanged() override { ui.dataChanged(); }
		View view() const override { return ui.view(); }
		int systemIndex() const override { return ui.systemIndex(); }
		int gameIndex() const override { return ui.gameIndex(); }
		bool press(Button b) override { return ui.press(b); }
		// (the engine's bar: the logo and the version on the left, the date and the time on the right; the pill halfway
		// between them, where its middle is free)
		void timeLeftAt(float& x, float& y, float& room) const override
		{
			x = std::round((rnds::statusFreeFrom + rnds::statusFreeTo) / 2); y = 32;
			room = rnds::statusFreeTo - rnds::statusFreeFrom - 16;	// (8 clear on each side)
		}
		bool touch(int screen, float x, float y, bool down) override { return ui.touch(screen, x, y, down); }
		bool touchMove(int screen, float x, float y) override { return ui.touchMove(screen, x, y); }
		void touchCancel() override { ui.touchCancel(); }
		void update(double nowMs) override { ui.update(nowMs); }
		void render() override { ui.render(); }
		int nextWake() const override { return ui.nextWake(); }
		bool workOnce() override { return ui.workOnce(); }
		void finishWork() override { ui.finishWork(); }
		void setNotifiers(std::function<void()> queued, std::function<void()> finished) override { ui.setNotifiers(queued, finished); }
		bool animating() const override { return ui.animating(); }
		void setLog(std::function<void(const std::string&)> log) override { ui.setLog(log); }
		// the engine's jobs are self-contained (they hold copies of what they paint from) and share only its queue,
		// under its own lock: run under the frames' lock, a picture's decode and fitting held a frame back for as long
		bool ownLocking() const override { return true; }
		rnds::UI& engine() { return ui; }
	private:
		rnds::UI ui;
	};
}
