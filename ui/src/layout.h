#pragma once
// banana-ui's layout themes: a menu drawn entirely from a theme's JSON. The theme describes each screen of each view
// (home, library, launch) as a tree of elements (rect, text, image, group, list) whose every property can be a value,
// an expression or a text template over the menu's hooks: the systems, the games, the selection and its eased scroll,
// the clock, the battery, the time since the last move... The engine only supplies the data, the input and the
// drawing; how anything looks and moves is the theme's. docs/THEMES.md describes the format.
#include "expr.h"
#include "json.h"
#include "menu.h"
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

namespace banana
{
	// a user theme's JSON file with a "screens" member (theme.cpp's pathFor): a layout theme
	bool isLayoutTheme(const std::string& path);

	class LayoutMenu : public Menu
	{
	public:
		// path: the theme's .json; fontDirs: where the built-in fonts are (rnds' Pixelify Sans, Banana's Inter)
		LayoutMenu(rnds::Source* src, rnds::Backend* be, const std::string& path, float scale,
			const std::string& pixelFonts, const std::string& interFonts);
		~LayoutMenu();
		void setHome(int sys) override;
		void setLibrary(int sys, int game) override;
		void dataChanged() override;
		View view() const override { return mView; }
		int systemIndex() const override { return mSys; }
		int gameIndex() const override { return mGame; }
		bool press(Button b) override;
		bool touch(int screen, float x, float y, bool down) override;
		bool touchMove(int screen, float x, float y) override;
		void touchCancel() override { mDown = false; }
		void update(double nowMs) override;
		void render() override;
		int nextWake() const override;
		bool workOnce() override;
		void finishWork() override { while (workOnce()) {} }
		void setNotifiers(std::function<void()> queued, std::function<void()> finished) override { mQueued = queued; mFinished = finished; }
		bool animating() const override;
		void setLog(std::function<void(const std::string&)> log) override;
		bool ownLocking() const override { return true; }	// (the jobs share only the queue, under jm)
		// what was wrong with the theme (shown on the top screen with "debug": true, and logged)
		const std::vector<std::string>& errors() const { return mErrors; }

		struct Node;
		struct Impl;
	private:
		std::unique_ptr<Impl> d;
		rnds::Source* src;
		rnds::Backend* be;
		View mView = rnds::UI::HOME;
		int mSys = 0, mGame = 0;
		bool mDown = false;
		float downX = 0, downY = 0;
		int downScreen = 0;
		std::function<void()> mQueued, mFinished;
		std::function<void(const std::string&)> mLog;
		std::vector<std::string> mErrors;
		mutable std::mutex jm;
	};
}
