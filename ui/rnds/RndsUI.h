#pragma once
// ROCKNIXDS rnds engine: the ROCKNIXDS menu mockup (mockup/index.html) as a native two-screen UI. Three views: home
// (the system shelf), library (a system's games as cartridges) and the ready screen. Layout, colours, fonts and
// animations follow the mockup's CSS; positions are CSS px of a 640x480 screen and are scaled to the panel (x1.6 on
// the RG DS Plus). The UI knows nothing about EmulationStation: a Source supplies systems, games and status, a Backend
// draws textures. In ES those are SystemData/FileData and the GL renderer; in the test harness the mockup's own data
// and a software compositor.
#include "RndsRaster.h"
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rnds
{
	// ---- data ---------------------------------------------------------------------------------------------------------
	struct SysInfo
	{
		std::string id;			// icon/accent key
		std::string name;		// "Nintendo DS"
		std::string maker;		// "Nintendo"
		Color accent;
		std::string icon;		// 32x32 pixel icon (PNG path)
		int games = 0, played = 0;
		std::string time;		// "86h 12m"
		bool fav = false;
		bool app = false;			// the system is one app (the theme's systems.cfg says "[app]"): A on its tile starts it
		bool hasResume = false;		// a game to show on the "last played" chip
		std::string resumeEyebrow;	// "Last played"
		std::string resumeTitle;
		std::string resumeBox;		// the chip's cartridge: box cover, the real cart scan, the label art (see GameInfo)
		std::string resumeCart, resumeLabel;
	};

	struct GameInfo
	{
		std::string title;
		std::string sub;		// bubble subtitle: "Role-playing · 2009"
		std::string plays;		// top screen stats: "14", "Yesterday", "22h 10m"
		std::string last;
		std::string time;
		std::string box;		// the 2D box cover (top screen)
		std::string cart;		// the real cartridge scan (its label art fills the drawn cartridge)
		std::string label;		// label art made from the cover (the media tool's "boxback"), when there is no scan
		std::string rom;		// a DS ROM (.nds, .zip) whose own icon goes on the bottom screen's cartridge instead
								// of the art above (the host's opt-in setting; the art when the ROM has no icon)
		std::string icon;		// what a cartridge with none of the art above shows on a plain label instead of
								// staying empty: a 32x32 pixel icon, its system's (a tool, an app, a game nobody scraped)
		int achA = 0, achB = 0;	// unlocked / total
		std::string achLabel;	// "28 / 50 achievements" (lower case; drawn upper)
		bool fav = false;
	};

	class Source
	{
	public:
		virtual ~Source() {}
		virtual int systemCount() = 0;
		virtual SysInfo system(int i) = 0;
		virtual int gameCount() = 0;			// the library's list (the current system)
		virtual GameInfo game(int i) = 0;
		virtual std::string clockText() = 0;	// "01:01 PM" / "13:01"
		virtual std::string dateText() = 0;		// "10/02"
		virtual float battery() = 0;			// 0..1, < 0 unknown
		virtual std::string batteryMode() { return "icon"; }	// ES's "show battery status": "" none, "icon", "text" (icon and %)
		virtual bool topMotion() { return true; }	// false: the top screen holds still (no rise on a move, no bob)
		virtual bool online() = 0;

		// actions
		virtual void selectSystem(int i) = 0;	// the home cursor moved
		virtual void selectGame(int i) = 0;		// the library cursor moved
		virtual int openLibrary(int sys) = 0;	// home -> library: the game index to start on
		virtual void backToHome() = 0;			// library -> home
		virtual void launch(int game) = 0;		// the ready screen's game (library index)
		virtual void toggleSystemFav(int sys) = 0;
		virtual void toggleGameFav(int game) = 0;
		virtual int resumeGame(int sys) = 0;	// library index of the system's resume game, opening that list; -1 none
		virtual void cue(const std::string& /*name*/) {}	// a theme sound for a moment of an animation ("insert")
		// the library with no games: what the bubble says ("No games yet") and the hint under it ("Copy .nds files to
		// roms/nds"); the host knows where the games go
		virtual std::string emptyTitle() { return "No games yet"; }
		virtual std::string emptyHint() { return ""; }
	};

	// ---- drawing ------------------------------------------------------------------------------------------------------
	struct Tex
	{
		virtual ~Tex() {}
		int w = 0, h = 0;
		bool alpha = false;
	};
	typedef std::shared_ptr<Tex> TexPtr;

	class Backend
	{
	public:
		virtual ~Backend() {}
		virtual TexPtr upload(const Bitmap& b, bool smooth) = 0;
		// device pixels of the current screen; tint multiplies (alpha textures take its colour)
		virtual void draw(const TexPtr& t, float x, float y, float w, float h, const Color& tint) = 0;
		virtual void fill(float x, float y, float w, float h, const Color& c) = 0;
		virtual void pushClip(float x, float y, float w, float h) = 0;
		virtual void popClip() = 0;
		virtual void beginScreen(int screen) = 0;	// 0 top, 1 bottom
		virtual void endScreen() = 0;
	};

	// a texture plus where its CSS box sits in it (device px)
	struct Sprite
	{
		TexPtr tex;
		int ox = 0, oy = 0;		// the box's top-left inside the bitmap
		bool valid() const { return (bool)tex; }
	};

	// ---- timing functions -------------------------------------------------------------------------------------------
	float cubicBezier(float x1, float y1, float x2, float y2, float p);
	inline float stepsEnd(int n, float p) { return p >= 1 ? 1 : (p <= 0 ? 0 : (float)(int)(p * n) / n); }

	// ---- the UI -------------------------------------------------------------------------------------------------------
	// the accent: the selected plate, the scroll thumb, held icons, the settings' pick, and every yellow of the logo
	// and the boot animation (setAccent before the UI is made; Banana's settings: yellow, blue, red, green, purple,
	// pink)
	extern std::string brandNote;	// beside the logo at the top (Banana: "Alpha 0.3.0"); empty: nothing
	struct Accent { uint32_t base, light, dark, band, low; };
	extern Accent accent;
	bool setAccent(const std::string& name);		// false: unknown, the yellow kept
	void tintAccent(Bitmap& b);						// RGBA8: its yellows in the accent's hue (nothing with the yellow)
	uint32_t tintAccent(uint32_t argb);				// one colour, 0xAARRGGBB (premultiplied is fine: hue and ratios)

	class UI
	{
	public:
		enum View { HOME, LIBRARY, LAUNCH };

		// variant: "dark" or "light" (the theme's palette)
		UI(Source* src, Backend* be, const std::string& assetDir, float scale, const std::string& variant = "dark");
		~UI();

		// state changes driven by the host
		void setHome(int sys);					// showing home, cursor on sys
		void setLibrary(int sys, int game);		// showing sys's library, cursor on game
		void setLaunch(int sys, int game, bool fromHome);
		void dataChanged();						// metadata / favourites / lists changed: rebuild texts
		void dropTextures();					// the GL context is going away (a game starts): forget every texture
		View view() const { return mView; }
		bool launchPending() const;				// the ready screen is waiting to start its game
		int systemIndex() const { return mSys; }
		int gameIndex() const { return mGame; }

		// input: true when used
		enum Button { LEFT, RIGHT, UP, DOWN, A, B, X, Y, L, R };
		bool press(Button b);
		void jumpTo(int game);					// the library's cursor straight to a game (jump to a letter)
		bool touch(int screen, float x, float y, bool down);	// device px of that screen
		bool touchMove(int screen, float x, float y);			// the finger moved while down (drags the game list's rail)
		void touchCancel();						// the view went under a menu: the touch ends where it is, nothing acts

		void update(double nowMs);
		void render();

		// ms until something must be drawn again: 0 = every frame, -1 = nothing pending
		int nextWake() const;

		// asynchronous image work: the host's thread (or the harness) calls this until it returns false
		bool workOnce();
		void finishWork();	// harness: run every queued job now
		// called (from any thread) when a job is queued, and when one is finished
		void setNotifiers(std::function<void()> queued, std::function<void()> finished);
		bool animating() const;
		bool settled() const;	// nothing queued, nothing waiting to be uploaded, the last frame was complete

		// the texture cache: entries and bytes per group (text, boxes, icons, cartridges, photos), its byte budget
		// (scaled with the panel's area), what the last frame drew, and the worker's queues
		struct CacheStats
		{
			size_t entries = 0, bytes = 0, budget = 0, frameEntries = 0, frameBytes = 0;
			size_t groupEntries[5] = {}, groupBytes[5] = {};
			size_t jobs = 0, done = 0, doneBytes = 0, queued = 0;
		};
		CacheStats cacheStats() const;
		// one-off notes for the host's log (the cache reaching its byte budget)
		void setLog(std::function<void(const std::string&)> log);

	private:
		struct Impl;
		std::unique_ptr<Impl> d;
		View mView = HOME;
		int mSys = 0, mGame = 0;
	};
}
