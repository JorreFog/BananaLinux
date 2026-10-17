#pragma once
// banana-ui's Carts theme (experimental): the games as cartridges. The bottom screen is the shelf: a system's
// cartridges standing in a row, the chosen one lifted out of it, flipped through with the d-pad or a finger. The top
// screen shows the chosen cartridge large, turned to the light, beside its name, genre and year, play time and
// achievements, in a clean typeface (Inter). Home is the same with one cartridge per system. A starts the game: the
// cartridge drops from the top screen into a slot on the bottom one. The cartridges are rendered once (cart3d.cpp)
// on the picture worker's thread and kept.
#include "menu.h"
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

namespace banana
{
	class CartMenu : public Menu
	{
	public:
		// fontDir: Inter's TTFs (Banana's assets/banana/fonts); icons come with the library's systems
		CartMenu(rnds::Source* src, rnds::Backend* be, const std::string& fontDir, float scale);
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
		void setLog(std::function<void(const std::string&)> log) override { mLog = log; }
		bool ownLocking() const override { return true; }	// (the jobs only share the queue, under jm)

	private:
		rnds::Source* src;
		rnds::Backend* be;
		float s;					// device px per layout px (480 lines)
		View mView = rnds::UI::HOME;
		int mSys = 0, mGame = 0;
		double now = 0, last = 0;
		float pos = 0;				// the shelf's scroll, eased towards the cursor (items)
		double movedAt = -1e9;		// the cursor's last move (the chosen cartridge's lift)
		double heroAt = -1e9;		// the top screen's last change (its cartridge slides in)
		int heroDir = 1;
		double launchAt = -1;		// the launch animation's start (-1: none)
		int launchGame = -1;
		bool cued = false;
		bool mDown = false;
		float downX = 0, downY = 0, downPos = 0;
		bool dragged = false;
		std::function<void()> mQueued, mFinished;
		std::function<void(const std::string&)> mLog;

		// what is shown: home's systems, or the current system's games
		struct Item
		{
			std::string system;		// the cartridge's model ("gba", "nds", ...)
			std::string art;		// the label's picture (box art, else the title screen, a DS game's icon)
			std::string icon;		// the system's pixel icon (a plain label's mark)
			std::string band;		// the label band's text: the system's name
			std::string title, sub, eyebrow;
			std::string stat[3], statLabel[3];
			int achA = 0, achB = 0;
			bool fav = false, app = false;
			uint32_t accent = 0x4a8af0;
		};
		std::vector<Item> items;
		void rebuild();
		int count() const { return (int)items.size(); }
		int cursor() const { return mView == rnds::UI::HOME ? mSys : mGame; }
		void move(int to);
		float shelfX(int i) const;	// a cartridge's x on the bottom screen

		// pictures: cartridges rendered on the worker's threads, made textures there too (the software backend's
		// upload touches nothing but the new texture), and taken into the cache here
		struct Job
		{
			std::string key, system, art, icon;
			rnds::Bitmap band;				// the label band's text (rendered here: the fonts are the main thread's)
			int height = 0;
			float yaw = 0, pitch = 0;
			bool wall = false;				// a piece of the top screen's wall instead, lit by glow
			int back = -1;					// or a whole screen's background (0 top, 1 bottom) in accent
			uint32_t accent = 0, glow = 0;
			float heroH = 0;
		};
		std::mutex jm;
		std::deque<Job> jobs;
		std::set<std::string> queued;
		// a cartridge: its picture, its reflection in the glossy floor (starting at row reflTop of the picture, each
		// column mirrored where it stands) and the colour its art throws on the wall behind it
		struct CartTex { rnds::TexPtr tex, refl; int reflTop = 0; uint32_t glow = 0; };
		std::vector<std::pair<std::string, CartTex>> doneTex;	// (a wall: tex, and where it goes in reflTop and glow)
		std::map<std::string, CartTex> carts;
		std::deque<std::string> cartOrder;	// least recently wanted first (the cache is kept small)
		const CartTex* cart(const Item& it, int height, float yaw, float pitch);
		// a cartridge drawn standing on the floor at (cx, floorY) (layout px), raised by lift, with its reflection
		void drawCart(const CartTex& c, float cx, float floorY, float lift, float tint, float alpha, bool reflect);

		// text, rounded boxes, the backgrounds
		std::shared_ptr<rnds::Font> fReg, fMed, fSemi, fBold;
		struct TextTex { rnds::TexPtr tex; int padX, padY; float w; };
		std::map<std::string, TextTex> texts;
		std::map<std::string, rnds::TexPtr> shapes, backs;
		rnds::TexPtr lastBack[2];					// each screen's last background (shown while a new colour's is made)
		std::map<std::string, rnds::Bitmap> bands;	// the label bands' texts, by system name
		// the top screen's wall behind the cartridge with the light of its art on it (a few kept: the last ones seen)
		struct Wall { rnds::TexPtr tex; int x = 0, y = 0; };
		std::map<std::string, Wall> walls;
		std::deque<std::string> wallOrder;
		float text(const std::string& t, const std::shared_ptr<rnds::Font>& f, float size, float x, float y, uint32_t rgb,
			float alpha = 1, float maxW = 0, int align = 0, float spacing = 0, bool upper = false, bool measure = false);
		void round(float x, float y, float w, float h, float r, uint32_t rgb, float alpha);
		bool background(int screen, uint32_t accent);	// false: the last colour's, while this one is made
		void shadow(float cx, float y, float w, float alpha);
		float X(float v) const { return v * s; }

		void drawTop();
		void drawBottom();
	};
}
