#pragma once
// banana-ui: the boot animation, on both panels from the first frame while the library is read on another thread.
// Top: the pixel bunch drops in and bounces, BANANA types itself in, a shine runs over the bananas, LOADING ticks.
// Bottom: the BANANA cartridge comes down and goes into the console with the menu's own insert motion (lift,
// accelerating slide, click, sparks, the power light turning green). Everything moves in whole pixels and in
// steps, like ROCKNIXDS Pixel's animations. The still layers are tools/make-splash.mjs's.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace banana
{
	class Splash
	{
	public:
		// dir: the splash assets; banana: the pixel bunch (assets/banana/banana-48.png); variant: dark or light
		bool load(const std::string& dir, const std::string& banana, const std::string& variant, int w, int h);
		// both screens at t ms after the start; ready: the menu is ready to take over (stops the waiting loop)
		void draw(uint32_t* top, uint32_t* bot, double t, bool ready);
		// the intro has played to its end and the menu is ready: time to hand over
		bool finished(double t, bool ready) const { return ready && t >= INTRO_END; }
		bool played() const { return W > 0; }
		// ms until the next frame is different (16 while anything moves)
		int nextFrame(double t, bool ready) const;
		std::function<void(const std::string&)> onCue;	// "insert" when the cartridge clicks in

		static constexpr double INTRO_END = 1390;

	private:
		struct Rgba { int w = 0, h = 0; std::vector<uint32_t> px; };	// premultiplied 0xAARRGGBB
		struct Mask { int w = 0, h = 0; std::vector<uint8_t> a; std::vector<int> letters; };	// letters: first column of each
		int W = 0, H = 0, K = 2, KW = 6, KL = 2;	// device px per sprite pixel: the bunch, the word, LOADING
		float s = 1;
		bool light = false, cued = false;
		std::vector<uint32_t> bgTop, bgBot;
		Rgba cart, lip, bunch;
		Mask word, loading;
		uint32_t ink = 0, muted = 0;

		int X(float v) const;
		void drawBunch(uint32_t* d, int x, int y, double shine) const;
		void drawMask(uint32_t* d, const Mask& m, int k, int x, int y, uint32_t colour, int fromCol, int toCol, int dy) const;
		void drawWord(uint32_t* d, double t) const;
		void blit(uint32_t* d, const Rgba& r, int x, int y, int clipY1) const;
		void fill(uint32_t* d, int x, int y, int w, int h, uint32_t c) const;
	};
}
