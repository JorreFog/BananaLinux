#pragma once
// banana-ui: the menu of the theme in use, as the main loop drives it. Dark and Light are ROCKNIXDS Pixel's engine
// (rnds::UI, used as it is through RndsMenu); Carts is Banana's own (carts.cpp). The calls are rnds::UI's.
#include "../rnds/RndsUI.h"
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
		rnds::UI& engine() { return ui; }
	private:
		rnds::UI ui;
	};
}
