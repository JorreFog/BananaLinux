#include "glass.h"
#include "theme.h"
#include "../rnds/RndsUI.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace banana
{
	float boxDistance(float px, float py, float hw, float hh, const float rad[4], float grow)
	{
		float r = std::min(std::min(hw, hh), px < 0 ? (py < 0 ? rad[0] : rad[3]) : (py < 0 ? rad[1] : rad[2]));
		r = std::max(0.0f, r + grow);
		float qx = std::fabs(px) - (hw + grow) + r, qy = std::fabs(py) - (hh + grow) + r;
		float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
		return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
	}

	void boxBlur(rnds::Bitmap& b, int r)
	{
		if (r <= 0 || b.empty() || b.alpha) return;
		std::vector<uint8_t> tmp(b.px.size());
		auto pass = [&](const uint8_t* src, uint8_t* dst, int n, int lines, size_t step, size_t lineStep)
		{
			int win = 2 * r + 1;
			for (int l = 0; l < lines; l++)
			{
				const uint8_t* s0 = src + l * lineStep;
				uint8_t* d0 = dst + l * lineStep;
				for (int ch = 0; ch < 4; ch++)
				{
					// the edges held: the window reaches past them as the edge pixel repeated
					auto at = [&](int i) { return (int)s0[(size_t)std::min(std::max(i, 0), n - 1) * step + ch]; };
					int sum = 0;
					for (int i = -r; i <= r; i++) sum += at(i);
					for (int i = 0; i < n; i++)
					{
						d0[(size_t)i * step + ch] = (uint8_t)((sum + win / 2) / win);
						sum += at(i + r + 1) - at(i - r);
					}
				}
			}
		};
		for (int k = 0; k < 3; k++)
		{
			pass(b.px.data(), tmp.data(), b.w, b.h, 4, (size_t)b.w * 4);		// across
			pass(tmp.data(), b.px.data(), b.h, b.w, (size_t)b.w * 4, 4);		// down
		}
	}

	// (a few kept: each screen's picture sharp and blurred, for the menu and Settings. Never destroyed: a prewarm still
	// at work when the program ends would find them gone)
	struct Made { int screen; std::string path; int w, h, r; std::shared_ptr<const rnds::Bitmap> b; };
	static std::mutex& madeLock = *new std::mutex;
	static std::deque<Made>& made = *new std::deque<Made>;

	static std::shared_ptr<const rnds::Bitmap> backdrop(int screen, const std::string& p, int w, int h, int r)
	{
		std::lock_guard<std::mutex> l(madeLock);
		for (const Made& m : made)
			if (m.screen == screen && m.path == p && m.w == w && m.h == h && m.r == r) return m.b;
		rnds::Bitmap b = p.empty() || w <= 0 || h <= 0 ? rnds::Bitmap() : rnds::decodeFile(p);
		std::shared_ptr<const rnds::Bitmap> out;
		if (!b.empty() && !b.alpha)
		{
			b = rnds::resize(b, w, h);
			boxBlur(b, r);
			out = std::make_shared<const rnds::Bitmap>(std::move(b));
		}
		if (made.size() >= 6) made.pop_front();
		made.push_back(Made{ screen, p, w, h, r, out });
		return out;
	}

	std::shared_ptr<const rnds::Bitmap> glassBackdrop(int screen, int w, int h, int r)
	{
		if (screen < 0 || screen > 1) return nullptr;
		if (rnds::themeFiles.wallTop.empty() && rnds::themeFiles.wallBottom.empty())	// (another theme now: let them go)
		{
			std::lock_guard<std::mutex> l(madeLock);
			made.clear();
			return nullptr;
		}
		// (blurred: the theme's own frosted picture when it has one, only filled to the screen)
		const std::string& frost = screen == 0 ? glassLook.frostTop : glassLook.frostBottom;
		if (r > 0 && !frost.empty()) return backdrop(screen, frost, w, h, 0);
		return backdrop(screen, screen == 0 ? rnds::themeFiles.wallTop : rnds::themeFiles.wallBottom, w, h, r);
	}

	void glassPrewarm(int w, int h, int r)
	{
		// (read here, on the menu's thread)
		std::string top = rnds::themeFiles.wallTop, bottom = rnds::themeFiles.wallBottom, ft = glassLook.frostTop, fb = glassLook.frostBottom;
		if (top.empty() && bottom.empty()) return;
		std::thread([=]
		{
			backdrop(0, top, w, h, 0);
			backdrop(1, bottom, w, h, 0);
			if (ft.empty()) backdrop(0, top, w, h, r); else backdrop(0, ft, w, h, 0);
			if (fb.empty()) backdrop(1, bottom, w, h, r); else backdrop(1, fb, w, h, 0);
		}).detach();
	}

	rnds::Bitmap glassPiece(const rnds::Bitmap& bd, int x, int y, int w, int h, const float rad[4])
	{
		rnds::Bitmap b;
		if (bd.empty() || w <= 0 || h <= 0 || (size_t)w * h > 4096u * 4096u) return b;
		b.w = w; b.h = h;
		b.px.assign((size_t)w * h * 4, 0);
		float hw = w / 2.0f, hh = h / 2.0f;
		// only the corners need the rounded box's distance: everywhere else the piece is the picture, solid (a list's
		// rows make theirs again at each step while they scroll)
		int rc = (int)std::ceil(std::min(std::max(std::max(rad[0], rad[1]), std::max(rad[2], rad[3])), std::min(hw, hh))) + 1;
		for (int j = 0; j < h; j++)
		{
			int sy = std::min(std::max(y + j, 0), bd.h - 1);
			bool band = j < rc || j >= h - rc;
			for (int i = 0; i < w; i++)
			{
				if (!band || (i >= rc && i < w - rc))
				{
					int sx = std::min(std::max(x + i, 0), bd.w - 1);
					const uint8_t* src = &bd.px[((size_t)sy * bd.w + sx) * 4];
					uint8_t* o = &b.px[((size_t)j * w + i) * 4];
					o[0] = src[0]; o[1] = src[1]; o[2] = src[2]; o[3] = 255;
					continue;
				}
				float cov = std::min(1.0f, std::max(0.0f, 0.5f - boxDistance(i + 0.5f - hw, j + 0.5f - hh, hw, hh, rad, 0)));
				if (cov <= 0) continue;
				int sx = std::min(std::max(x + i, 0), bd.w - 1);
				const uint8_t* src = &bd.px[((size_t)sy * bd.w + sx) * 4];
				uint8_t* o = &b.px[((size_t)j * w + i) * 4];
				o[0] = src[0]; o[1] = src[1]; o[2] = src[2];
				o[3] = (uint8_t)std::lround(cov * 255);
			}
		}
		return b;
	}
}
