#include "soft.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace banana
{
	// all four channels of a premultiplied word times k/255, rounded
	static inline uint32_t mul255(uint32_t p, uint32_t k)
	{
		uint32_t rb = (p & 0x00ff00ffu) * k + 0x00800080u;
		rb = ((rb + ((rb >> 8) & 0x00ff00ffu)) >> 8) & 0x00ff00ffu;
		uint32_t ag = ((p >> 8) & 0x00ff00ffu) * k + 0x00800080u;
		ag = (ag + ((ag >> 8) & 0x00ff00ffu)) & 0xff00ff00u;
		return rb | ag;
	}

	// source-over, both premultiplied
	static inline uint32_t over(uint32_t s, uint32_t d)
	{
		uint32_t sa = s >> 24;
		if (sa == 255) return s;
		if (sa == 0 && s == 0) return d;
		return s + mul255(d, 255 - sa);
	}

	static inline uint32_t premul(const rnds::Color& c)
	{
		float a = std::min(1.0f, std::max(0.0f, c.a));
		auto ch = [a](float v) { return (uint32_t)std::lround(std::min(1.0f, std::max(0.0f, v)) * a * 255); };
		return ((uint32_t)std::lround(a * 255) << 24) | (ch(c.r) << 16) | (ch(c.g) << 8) | ch(c.b);
	}

	// a premultiplied word times a tint (straight colour and alpha)
	static inline uint32_t tinted(uint32_t p, uint32_t tr, uint32_t tg, uint32_t tb, uint32_t ta)
	{
		uint32_t a = p >> 24, r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
		auto m = [](uint32_t x, uint32_t y) { uint32_t t = x * y + 128; return (t + (t >> 8)) >> 8; };
		r = m(m(r, tr), ta); g = m(m(g, tg), ta); b = m(m(b, tb), ta); a = m(a, ta);
		return (a << 24) | (r << 16) | (g << 8) | b;
	}

	// a row's runs from a per-pixel kind
	// a row's runs from each pixel's alpha (or coverage): the byte at a[0], a[step], ... of each row, rows stride
	// bytes apart
	static void encodeRuns(SoftTex& t, const uint8_t* a, size_t step, size_t stride)
	{
		t.rowAt.resize(t.h + 1);
		t.runs.clear();
		t.runs.reserve((size_t)t.h * 4);
		for (int y = 0; y < t.h; y++)
		{
			t.rowAt[y] = (uint32_t)t.runs.size();
			const uint8_t* r = a + (size_t)y * stride;
			int x = 0;
			while (x < t.w)
			{
				uint8_t v = r[(size_t)x * step];
				int e = x + 1;
				uint32_t k;
				if (v == 0) { k = SoftTex::CLEAR; while (e < t.w && r[(size_t)e * step] == 0) e++; }
				else if (v == 255) { k = SoftTex::SOLID; while (e < t.w && r[(size_t)e * step] == 255) e++; }
				else { k = SoftTex::MIXED; while (e < t.w && (uint8_t)(r[(size_t)e * step] - 1) < 254) e++; }
				t.runs.push_back((k << 30) | (uint32_t)(e - x));
				x = e;
			}
		}
		t.rowAt[t.h] = (uint32_t)t.runs.size();
	}

	// the alpha byte of the premultiplied words' rows (the word's top byte: the fourth in memory)
	static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "the alpha byte is read from the words' memory");
	static void encodeRuns(SoftTex& t) { encodeRuns(t, (const uint8_t*)t.px.data() + 3, 4, (size_t)t.w * 4); }

	SoftBackend::SoftBackend(int w, int h) : W(w), H(h)
	{
		for (int s = 0; s < 2; s++)
		{
			scr[s].assign((size_t)W * H, 0xff000000u);
			dmg[s] = { 0, 0, W, H };
		}
	}

	rnds::TexPtr SoftBackend::upload(const rnds::Bitmap& b, bool smooth)
	{
		auto t = std::make_shared<SoftTex>();
		t->w = b.w; t->h = b.h; t->alpha = b.alpha; t->smooth = smooth;
		if (b.empty()) return t;
		size_t n = (size_t)b.w * b.h;
		if (b.alpha)
		{
			t->a8.assign(b.px.begin(), b.px.begin() + n);
			encodeRuns(*t, t->a8.data(), 1, (size_t)b.w);
			return t;
		}
		t->px.resize(n);
		bool opaque = true;
		const uint8_t* s = b.px.data();
		for (size_t i = 0; i < n; i++, s += 4)
		{
			uint32_t a = s[3];
			if (a != 255) opaque = false;
			auto m = [a](uint32_t v) { uint32_t x = v * a + 128; return (x + (x >> 8)) >> 8; };
			t->px[i] = (a << 24) | (m(s[0]) << 16) | (m(s[1]) << 8) | m(s[2]);
		}
		t->opaque = opaque;
		encodeRuns(*t);
		return t;
	}

	void SoftBackend::beginScreen(int screen)
	{
		cur = screen;
		rec[cur].clear();
	}

	// pixels whose centres fall in [v, v + len): GL's rasterization rule
	static inline int edge(float v) { return (int)std::ceil(v - 0.5f); }

	static inline bool overlaps(int ax0, int ay0, int ax1, int ay1, int bx0, int by0, int bx1, int by1)
	{
		return ax0 < bx1 && bx0 < ax1 && ay0 < by1 && by0 < ay1;
	}

	// the pixels a draw or fill can touch in its clip (empty: none)
	SoftBackend::Clip SoftBackend::bounds(const Cmd& c, const Clip& cl) const
	{
		Clip none = { 0, 0, 0, 0 };
		if (c.w <= 0 || c.h <= 0 || c.c[3] <= 0) return none;
		int bx0 = edge(c.x), by0 = edge(c.y), bx1 = edge(c.x + c.w), by1 = edge(c.y + c.h);
		if (c.op == Cmd::DRAW)
		{
			auto t = (const SoftTex*)c.tex.get();
			if (!t || t->w <= 0 || t->h <= 0) return none;
			if (std::fabs(c.w - t->w) < 0.01f && std::fabs(c.h - t->h) < 0.01f) { bx1 = bx0 + t->w; by1 = by0 + t->h; }
		}
		Clip r = { std::max(cl.x0, bx0), std::max(cl.y0, by0), std::min(cl.x1, bx1), std::min(cl.y1, by1) };
		return r.x0 < r.x1 && r.y0 < r.y1 ? r : none;
	}

	// the recorded list as draws with their boxes: clips resolved, what can't touch a pixel left out
	void SoftBackend::resolve()
	{
		items.clear();
		clips.clear();
		clips.push_back({ 0, 0, W, H });
		for (auto& c : rec[cur])
		{
			switch (c.op)
			{
			case Cmd::PUSH:
			{
				const Clip& p = clips.back();
				Clip n = { std::max(p.x0, edge(c.x)), std::max(p.y0, edge(c.y)), std::min(p.x1, edge(c.x + c.w)), std::min(p.y1, edge(c.y + c.h)) };
				if (n.x1 < n.x0) n.x1 = n.x0;
				if (n.y1 < n.y0) n.y1 = n.y0;
				clips.push_back(n);
				break;
			}
			case Cmd::POP: if (clips.size() > 1) clips.pop_back(); break;
			default:
			{
				Clip b = bounds(c, clips.back());
				if (b.x0 < b.x1) items.push_back({ std::move(c), b });
			}
			}
		}
	}

	// a rectangle to draw again: merged with the ones it overlaps, so each pixel is drawn once
	void SoftBackend::addDirty(const Clip& r0)
	{
		Clip r = r0;
		for (size_t i = 0; i < dirty.size();)
		{
			const Clip& d = dirty[i];
			if (overlaps(r.x0, r.y0, r.x1, r.y1, d.x0, d.y0, d.x1, d.y1))
			{
				r = { std::min(r.x0, d.x0), std::min(r.y0, d.y0), std::max(r.x1, d.x1), std::max(r.y1, d.y1) };
				dirty.erase(dirty.begin() + i);
				i = 0;	// the grown rectangle may reach ones already passed
			}
			else i++;
		}
		dirty.push_back(r);
	}

	// r from black (as ES's renderer clears before each frame: the screen's corners aren't covered by the engine),
	// every draw that touches it clipped to it
	void SoftBackend::redraw(const Clip& r)
	{
		uint32_t* base = scr[cur].data();
		for (int y = r.y0; y < r.y1; y++) std::fill(base + (size_t)y * W + r.x0, base + (size_t)y * W + r.x1, 0xff000000u);
		touch(r);
		redrawn += (uint64_t)(r.x1 - r.x0) * (r.y1 - r.y0);
		for (auto& it : items)
		{
			const Clip& b = it.box;
			if (!overlaps(b.x0, b.y0, b.x1, b.y1, r.x0, r.y0, r.x1, r.y1)) continue;
			Clip cl = { std::max(b.x0, r.x0), std::max(b.y0, r.y0), std::min(b.x1, r.x1), std::min(b.y1, r.y1) };
			const Cmd& c = it.c;
			rnds::Color col(c.c[0], c.c[1], c.c[2], c.c[3]);
			if (c.op == Cmd::DRAW) doDraw((SoftTex*)c.tex.get(), c.x, c.y, c.w, c.h, col, cl);
			else doFill(c.x, c.y, c.w, c.h, col, cl);
		}
	}

	void SoftBackend::endScreen()
	{
		if (overlay) overlay(cur);
		resolve();
		std::vector<Item>& old = last[cur];
		dirty.clear();
		if (full[cur])
		{
			dirty.push_back({ 0, 0, W, H });
			full[cur] = false;
		}
		else
		{
			// match the new list against the old one in order (a draw found a little further on means the ones
			// skipped over went away); what isn't matched is drawn again where it was and where it is
			const size_t window = 256;
			size_t p = 0;
			for (auto& it : items)
			{
				size_t e = std::min(old.size(), p + window), j = p;
				while (j < e && !(old[j] == it)) j++;
				if (j < e)
				{
					for (; p < j; p++) addDirty(old[p].box);
					p = j + 1;
				}
				else addDirty(it.box);
			}
			for (; p < old.size(); p++) addDirty(old[p].box);
			if (dirty.empty()) { skipped++; std::swap(items, old); return; }
		}
		for (auto& r : dirty) redraw(r);
		std::swap(items, old);
	}

	void SoftBackend::draw(const rnds::TexPtr& t, float x, float y, float w, float h, const rnds::Color& tint)
	{
		rec[cur].push_back({ Cmd::DRAW, t, x, y, w, h, { tint.r, tint.g, tint.b, tint.a } });
	}

	void SoftBackend::fill(float x, float y, float w, float h, const rnds::Color& c)
	{
		rec[cur].push_back({ Cmd::FILL, nullptr, x, y, w, h, { c.r, c.g, c.b, c.a } });
	}

	void SoftBackend::pushClip(float x, float y, float w, float h)
	{
		rec[cur].push_back({ Cmd::PUSH, nullptr, x, y, w, h, { 0, 0, 0, 0 } });
	}

	void SoftBackend::popClip()
	{
		rec[cur].push_back({ Cmd::POP, nullptr, 0, 0, 0, 0, { 0, 0, 0, 0 } });
	}

	void SoftBackend::doFill(float x, float y, float w, float h, const rnds::Color& c, const Clip& cl)
	{
		int x0 = std::max(cl.x0, edge(x)), y0 = std::max(cl.y0, edge(y));
		int x1 = std::min(cl.x1, edge(x + w)), y1 = std::min(cl.y1, edge(y + h));
		if (x0 >= x1 || y0 >= y1) return;
		uint32_t p = premul(c);
		for (int yy = y0; yy < y1; yy++)
		{
			uint32_t* d = &scr[cur][(size_t)yy * W];
			if ((p >> 24) == 255)
				std::fill(d + x0, d + x1, p);
			else
			{
				// the same colour over each pixel: the source's share is a constant
				uint32_t k = 255 - (p >> 24);
				for (int xx = x0; xx < x1; xx++) d[xx] = p + mul255(d[xx], k);
			}
		}
	}

	// a premultiplied word's channels spread into 32-bit lanes: red and blue, alpha and green
	static inline uint64_t lanesRB(uint32_t p) { return (uint64_t)(p & 0xffu) | ((uint64_t)(p & 0xff0000u) << 16); }
	static inline uint64_t lanesAG(uint32_t p) { return (uint64_t)((p >> 8) & 0xffu) | ((uint64_t)((p >> 8) & 0xff0000u) << 16); }

	// coverage between four texels, fx and fy in 1/256
	static inline uint32_t bilinearA8(const uint8_t* r0, const uint8_t* r1, int a, int b, uint32_t fx, uint32_t fy)
	{
		uint32_t top = r0[a] * (256 - fx) + r0[b] * fx, bot = r1[a] * (256 - fx) + r1[b] * fx;
		return (top * (256 - fy) + bot * fy) >> 16;
	}

	// a premultiplied word between four texels: the four weights sum to 65536, so each channel's weighted sum fits
	// a 32-bit lane and two channels go through each multiply
	static inline uint32_t bilinear(const uint32_t* r0, const uint32_t* r1, int a, int b, uint32_t fx, uint32_t fy)
	{
		uint64_t wa = (256 - fx) * (256 - fy), wb = fx * (256 - fy), wc = (256 - fx) * fy, wd = fx * fy;
		uint32_t pa = r0[a], pb = r0[b], pc = r1[a], pd = r1[b];
		const uint64_t half = 0x0000800000008000ull, lane = 0x000000ff000000ffull;
		uint64_t rb = ((lanesRB(pa) * wa + lanesRB(pb) * wb + lanesRB(pc) * wc + lanesRB(pd) * wd + half) >> 16) & lane;
		uint64_t ag = ((lanesAG(pa) * wa + lanesAG(pb) * wb + lanesAG(pc) * wc + lanesAG(pd) * wd + half) >> 16) & lane;
		return (uint32_t)rb | (uint32_t)(rb >> 16) | ((uint32_t)ag << 8) | ((uint32_t)(ag >> 16) << 8);
	}

	// a smooth RGBA texture's columns blended down to each row of a draw's box [by0, by1) (see SoftTex::vLanes)
	const uint64_t* SoftBackend::vertical(SoftTex* t, float y, float h, int64_t stepY, int by0, int by1)
	{
		double phY = (by0 + 0.5) - y;
		int rows = by1 - by0;
		auto same = [&](const SoftTex::Rows& r) { return r.phY == phY && r.h == h && r.rows == rows; };
		if (same(t->vRows[0])) return t->vRows[0].lanes.data();
		std::swap(t->vRows[0], t->vRows[1]);	// most recent first
		if (same(t->vRows[0])) return t->vRows[0].lanes.data();
		SoftTex::Rows& r = t->vRows[0];
		r.phY = phY; r.h = h; r.rows = rows;
		r.lanes.resize((size_t)rows * t->w * 2);
		for (int j = 0; j < rows; j++)
		{
			int64_t sv = (int64_t)(((by0 + j + 0.5) - y) * stepY) - 32768;
			int syA = (int)(sv >> 16); uint64_t fy = (uint64_t)((sv >> 8) & 255);
			if (sv < 0) { syA = 0; fy = 0; }
			int syB = std::min(syA + 1, t->h - 1);
			syA = std::min(syA, t->h - 1);
			const uint32_t* r0 = &t->px[(size_t)syA * t->w];
			const uint32_t* r1 = &t->px[(size_t)syB * t->w];
			uint64_t* o = &r.lanes[(size_t)j * t->w * 2];
			for (int i = 0; i < t->w; i++)
			{
				o[2 * i] = lanesRB(r0[i]) * (256 - fy) + lanesRB(r1[i]) * fy;
				o[2 * i + 1] = lanesAG(r0[i]) * (256 - fy) + lanesAG(r1[i]) * fy;
			}
		}
		return r.lanes.data();
	}

	// the source columns of n pixels from x0 of a draw at x whose box starts at bx0 (16.16 steps from pixel centres,
	// counted from the box, so a pixel samples the same texels however the draw is clipped)
	void SoftBackend::columns(const SoftTex* t, float x, int64_t stepX, int bx0, int x0, int n)
	{
		int64_t su0 = (int64_t)(((bx0 + 0.5) - x) * stepX) - (t->smooth ? 32768 : 0) + (int64_t)(x0 - bx0) * stepX;
		colA.resize(n); colB.resize(n); colF.resize(n);
		for (int i = 0; i < n; i++)
		{
			int64_t su = su0 + (int64_t)i * stepX;
			if (!t->smooth) { colA[i] = std::min(std::max((int)(su >> 16), 0), t->w - 1); continue; }
			int sxA = (int)(su >> 16); uint32_t fx = (uint32_t)((su >> 8) & 255);
			if (su < 0) { sxA = 0; fx = 0; }
			colB[i] = std::min(sxA + 1, t->w - 1);
			colA[i] = std::min(sxA, t->w - 1);
			colF[i] = fx;
		}
	}

	// the texture's samples for the pixels [x0, x1) x [y0, y1) of a draw at (x, y) w x h, untinted: premultiplied words
	// (RGBA) or coverage (alpha) into rows of stride pixels. What the scaled draw below blends, pixel for pixel
	void SoftBackend::sample(const SoftTex* t, float x, float y, float w, float h, int x0, int y0, int x1, int y1, uint32_t* px, uint8_t* a8, int stride)
	{
		int64_t stepX = (int64_t)((double)t->w / w * 65536.0), stepY = (int64_t)((double)t->h / h * 65536.0);
		int n = x1 - x0;
		columns(t, x, stepX, x0, x0, n);	// [x0, x1) is the draw's whole box
		for (int yy = y0; yy < y1; yy++)
		{
			size_t o = (size_t)(yy - y0) * stride;
			int64_t sv = (int64_t)(((yy + 0.5) - y) * stepY) - (t->smooth ? 32768 : 0);
			if (!t->smooth)
			{
				int sy = std::min(std::max((int)(sv >> 16), 0), t->h - 1);
				if (t->alpha) { const uint8_t* row = &t->a8[(size_t)sy * t->w]; for (int i = 0; i < n; i++) a8[o + i] = row[colA[i]]; }
				else { const uint32_t* row = &t->px[(size_t)sy * t->w]; for (int i = 0; i < n; i++) px[o + i] = row[colA[i]]; }
				continue;
			}
			int syA = (int)(sv >> 16); uint32_t fy = (uint32_t)((sv >> 8) & 255);
			if (sv < 0) { syA = 0; fy = 0; }
			int syB = std::min(syA + 1, t->h - 1);
			syA = std::min(syA, t->h - 1);
			if (t->alpha)
			{
				const uint8_t* r0 = &t->a8[(size_t)syA * t->w];
				const uint8_t* r1 = &t->a8[(size_t)syB * t->w];
				for (int i = 0; i < n; i++) a8[o + i] = (uint8_t)bilinearA8(r0, r1, colA[i], colB[i], colF[i], fy);
			}
			else
			{
				const uint32_t* r0 = &t->px[(size_t)syA * t->w];
				const uint32_t* r1 = &t->px[(size_t)syB * t->w];
				for (int i = 0; i < n; i++) px[o + i] = bilinear(r0, r1, colA[i], colB[i], colF[i], fy);
			}
		}
	}

	// a texture at its own size with its top-left at (bx0, by0), over the pixels [x0, x1) x [y0, y1): its rows' runs
	void SoftBackend::blitRuns(const SoftTex* t, int bx0, int by0, int x0, int y0, int x1, int y1, uint32_t tr, uint32_t tg, uint32_t tb, uint32_t ta, uint32_t tintWord)
	{
		bool plain = tr == 255 && tg == 255 && tb == 255 && ta == 255;
		// the visible columns [cx0, cx1) of the texture
		int cx0 = x0 - bx0, cx1 = x1 - bx0;
		bool solidTint = (tintWord >> 24) == 255;
		const uint32_t* runs = t->runs.data();
		for (int yy = y0; yy < y1; yy++)
		{
			uint32_t* d = &scr[cur][(size_t)yy * W + bx0];
			int sy = yy - by0;
			int x = 0;
			uint32_t r = t->rowAt[sy], rEnd = t->rowAt[sy + 1];
			// runs wholly left of the visible columns
			for (; r < rEnd; r++)
			{
				int len = (int)(runs[r] & 0x3fffffffu);
				if (x + len > cx0) break;
				x += len;
			}
			for (; r < rEnd && x < cx1; r++)
			{
				int len = (int)(runs[r] & 0x3fffffffu), kind = (int)(runs[r] >> 30);
				int a = std::max(x, cx0), e = std::min(x + len, cx1);
				x += len;
				if (kind == SoftTex::CLEAR) continue;
				if (t->alpha)
				{
					const uint8_t* s = &t->a8[(size_t)sy * t->w];
					if (kind == SoftTex::SOLID && solidTint) std::fill(d + a, d + e, tintWord);
					else for (int i = a; i < e; i++) d[i] = over(mul255(tintWord, s[i]), d[i]);
				}
				else
				{
					const uint32_t* s = &t->px[(size_t)sy * t->w];
					if (plain && kind == SoftTex::SOLID) memcpy(d + a, s + a, (size_t)(e - a) * 4);
					else if (plain)
					{
						// a mixed run has no clear or solid pixels: always a blend
						for (int i = a; i < e; i++) d[i] = s[i] + mul255(d[i], 255 - (s[i] >> 24));
					}
					else for (int i = a; i < e; i++) d[i] = over(tinted(s[i], tr, tg, tb, ta), d[i]);
				}
			}
		}
	}

	void SoftBackend::doDraw(SoftTex* t, float x, float y, float w, float h, const rnds::Color& tint, const Clip& cl)
	{
		int bx0 = edge(x), by0 = edge(y), bx1 = edge(x + w), by1 = edge(y + h);
		bool exact = std::fabs(w - t->w) < 0.01f && std::fabs(h - t->h) < 0.01f;
		if (exact) { bx1 = bx0 + t->w; by1 = by0 + t->h; }
		int x0 = std::max(cl.x0, bx0), y0 = std::max(cl.y0, by0);
		int x1 = std::min(cl.x1, bx1), y1 = std::min(cl.y1, by1);
		if (x0 >= x1 || y0 >= y1) return;

		auto q = [](float v) { return (uint32_t)std::lround(std::min(1.0f, std::max(0.0f, v)) * 255); };
		uint32_t tr = q(tint.r), tg = q(tint.g), tb = q(tint.b), ta = q(tint.a);
		uint32_t tintWord = premul(tint);	// what an alpha texture's coverage scales
		if (exact) { blitRuns(t, bx0, by0, x0, y0, x1, y1, tr, tg, tb, ta, tintWord); return; }

		// scaled at a size and phase it was scaled at before: the samples kept from then (the tint is applied as
		// they are drawn, as below)
		SoftTex::Scaled key = { (bx0 + 0.5) - x, (by0 + 0.5) - y, w, h, bx1 - bx0, by1 - by0, nullptr };
		size_t k = 0;
		while (k < t->scaled.size() && !t->scaled[k].same(key)) k++;
		if (k < t->scaled.size())
		{
			std::rotate(t->scaled.begin(), t->scaled.begin() + k, t->scaled.begin() + k + 1);	// most recent first
			SoftTex::Scaled& e = t->scaled[0];
			if (!e.tex && (int64_t)key.bw * key.bh <= (int64_t)W * H / 4)
			{
				// the second time: sample the whole box once
				auto st = std::make_shared<SoftTex>();
				st->w = key.bw; st->h = key.bh; st->alpha = t->alpha;
				if (t->alpha) st->a8.resize((size_t)st->w * st->h); else st->px.resize((size_t)st->w * st->h);
				sample(t, x, y, w, h, bx0, by0, bx1, by1, st->px.data(), st->a8.data(), st->w);
				if (t->alpha) encodeRuns(*st, st->a8.data(), 1, (size_t)st->w);
				else encodeRuns(*st);
				e.tex = st;
			}
			if (e.tex) { blitRuns(e.tex.get(), bx0, by0, x0, y0, x1, y1, tr, tg, tb, ta, tintWord); return; }
		}
		else
		{
			// the first time: remembered, drawn directly
			const size_t keep = 8;
			if (t->scaled.size() >= keep) t->scaled.pop_back();
			t->scaled.insert(t->scaled.begin(), key);
		}
		bool plain = tr == 255 && tg == 255 && tb == 255 && ta == 255;

		// scaled: each column's source position, then each row's
		int64_t stepX = (int64_t)((double)t->w / w * 65536.0), stepY = (int64_t)((double)t->h / h * 65536.0);
		int n = x1 - x0;
		columns(t, x, stepX, bx0, x0, n);
		const uint64_t* vrows = t->smooth && !t->alpha ? vertical(t, y, h, stepY, by0, by1) : nullptr;
		for (int yy = y0; yy < y1; yy++)
		{
			uint32_t* d = &scr[cur][(size_t)yy * W + x0];
			int64_t sv = (int64_t)(((yy + 0.5) - y) * stepY) - (t->smooth ? 32768 : 0);
			if (!t->smooth)
			{
				int sy = std::min(std::max((int)(sv >> 16), 0), t->h - 1);
				if (t->alpha)
				{
					const uint8_t* row = &t->a8[(size_t)sy * t->w];
					for (int i = 0; i < n; i++) { uint8_t c = row[colA[i]]; if (c) d[i] = over(mul255(tintWord, c), d[i]); }
				}
				else
				{
					const uint32_t* row = &t->px[(size_t)sy * t->w];
					for (int i = 0; i < n; i++) { uint32_t s = row[colA[i]]; if (!plain) s = tinted(s, tr, tg, tb, ta); d[i] = over(s, d[i]); }
				}
				continue;
			}
			int syA = (int)(sv >> 16); uint32_t fy = (uint32_t)((sv >> 8) & 255);
			if (sv < 0) { syA = 0; fy = 0; }
			int syB = std::min(syA + 1, t->h - 1);
			syA = std::min(syA, t->h - 1);
			if (t->alpha)
			{
				const uint8_t* r0 = &t->a8[(size_t)syA * t->w];
				const uint8_t* r1 = &t->a8[(size_t)syB * t->w];
				for (int i = 0; i < n; i++)
				{
					uint32_t cov = bilinearA8(r0, r1, colA[i], colB[i], colF[i], fy);
					if (cov) d[i] = over(mul255(tintWord, cov), d[i]);
				}
				continue;
			}
			// the weighted sum split exactly: ((256 - fx) * V(a) + fx * V(b) + 32768) >> 16 with V a column's two
			// texels times (256 - fy) and fy
			const uint64_t* v = &vrows[(size_t)(yy - by0) * t->w * 2];
			const uint64_t half = 0x0000800000008000ull, lane = 0x000000ff000000ffull;
			for (int i = 0; i < n; i++)
			{
				uint64_t fx = colF[i], gx = 256 - fx;
				const uint64_t* va = v + 2 * colA[i];
				const uint64_t* vb = v + 2 * colB[i];
				uint64_t rb = ((va[0] * gx + vb[0] * fx + half) >> 16) & lane;
				uint64_t ag = ((va[1] * gx + vb[1] * fx + half) >> 16) & lane;
				uint32_t s = (uint32_t)rb | (uint32_t)(rb >> 16) | ((uint32_t)ag << 8) | ((uint32_t)(ag >> 16) << 8);
				if (!plain) s = tinted(s, tr, tg, tb, ta);
				d[i] = over(s, d[i]);
			}
		}
	}
}
