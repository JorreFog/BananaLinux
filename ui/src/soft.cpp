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
	template<typename K> static void encodeRuns(SoftTex& t, K kindAt)
	{
		t.rowAt.resize(t.h + 1);
		t.runs.clear();
		for (int y = 0; y < t.h; y++)
		{
			t.rowAt[y] = (uint32_t)t.runs.size();
			int x = 0;
			while (x < t.w)
			{
				uint32_t k = kindAt(x, y);
				int e = x + 1;
				while (e < t.w && kindAt(e, y) == k) e++;
				t.runs.push_back((k << 30) | (uint32_t)(e - x));
				x = e;
			}
		}
		t.rowAt[t.h] = (uint32_t)t.runs.size();
	}

	SoftBackend::SoftBackend(int w, int h) : W(w), H(h)
	{
		for (int s = 0; s < 2; s++)
		{
			scr[s].assign((size_t)W * H, 0xff000000u);
			dmg0[s] = 0; dmg1[s] = H;
		}
	}

	void SoftBackend::touch(int y0, int y1)
	{
		if (y0 >= y1) return;
		if (dmg0[cur] >= dmg1[cur]) { dmg0[cur] = y0; dmg1[cur] = y1; return; }
		dmg0[cur] = std::min(dmg0[cur], y0);
		dmg1[cur] = std::max(dmg1[cur], y1);
	}

	void SoftBackend::takeDamage(int screen, int& y0, int& y1)
	{
		y0 = dmg0[screen]; y1 = dmg1[screen];
		dmg0[screen] = dmg1[screen] = 0;
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
			const uint8_t* a = t->a8.data();
			int w = b.w;
			encodeRuns(*t, [a, w](int x, int y) { uint8_t c = a[(size_t)y * w + x]; return c == 0 ? SoftTex::CLEAR : (c == 255 ? SoftTex::SOLID : SoftTex::MIXED); });
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
		const uint32_t* p = t->px.data();
		int w = b.w;
		encodeRuns(*t, [p, w](int x, int y) { uint32_t a = p[(size_t)y * w + x] >> 24; return a == 0 ? SoftTex::CLEAR : (a == 255 ? SoftTex::SOLID : SoftTex::MIXED); });
		return t;
	}

	void SoftBackend::beginScreen(int screen)
	{
		cur = screen;
		rec[cur].clear();
	}

	void SoftBackend::endScreen()
	{
		if (rec[cur] == last[cur]) { skipped++; return; }
		// from black, as ES's renderer clears before each frame (the screen's corners aren't covered by the engine)
		std::fill(scr[cur].begin(), scr[cur].end(), 0xff000000u);
		touch(0, H);
		clips.clear();
		clips.push_back({ 0, 0, W, H });
		for (auto& c : rec[cur]) run(c);
		std::swap(rec[cur], last[cur]);
	}

	void SoftBackend::run(const Cmd& c)
	{
		rnds::Color col(c.c[0], c.c[1], c.c[2], c.c[3]);
		switch (c.op)
		{
		case Cmd::DRAW: doDraw(c.tex, c.x, c.y, c.w, c.h, col); break;
		case Cmd::FILL: doFill(c.x, c.y, c.w, c.h, col); break;
		case Cmd::PUSH: doPushClip(c.x, c.y, c.w, c.h); break;
		case Cmd::POP: if (clips.size() > 1) clips.pop_back(); break;
		}
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

	// pixels whose centres fall in [v, v + len): GL's rasterization rule
	static inline int edge(float v) { return (int)std::ceil(v - 0.5f); }

	void SoftBackend::doPushClip(float x, float y, float w, float h)
	{
		Clip c = clips.back();
		Clip n = { std::max(c.x0, edge(x)), std::max(c.y0, edge(y)), std::min(c.x1, edge(x + w)), std::min(c.y1, edge(y + h)) };
		if (n.x1 < n.x0) n.x1 = n.x0;
		if (n.y1 < n.y0) n.y1 = n.y0;
		clips.push_back(n);
	}

	void SoftBackend::doFill(float x, float y, float w, float h, const rnds::Color& c)
	{
		if (w <= 0 || h <= 0 || c.a <= 0) return;
		const Clip& cl = clips.back();
		int x0 = std::max(cl.x0, edge(x)), y0 = std::max(cl.y0, edge(y));
		int x1 = std::min(cl.x1, edge(x + w)), y1 = std::min(cl.y1, edge(y + h));
		if (x0 >= x1 || y0 >= y1) return;
		touch(y0, y1);
		uint32_t p = premul(c);
		for (int yy = y0; yy < y1; yy++)
		{
			uint32_t* d = &scr[cur][(size_t)yy * W];
			if ((p >> 24) == 255)
				std::fill(d + x0, d + x1, p);
			else
				for (int xx = x0; xx < x1; xx++) d[xx] = over(p, d[xx]);
		}
	}

	void SoftBackend::doDraw(const rnds::TexPtr& tp, float x, float y, float w, float h, const rnds::Color& tint)
	{
		auto t = (const SoftTex*)tp.get();
		if (!t || t->w <= 0 || t->h <= 0 || w <= 0 || h <= 0 || tint.a <= 0) return;
		const Clip& cl = clips.back();
		int bx0 = edge(x), by0 = edge(y), bx1 = edge(x + w), by1 = edge(y + h);
		bool exact = std::fabs(w - t->w) < 0.01f && std::fabs(h - t->h) < 0.01f;
		if (exact) { bx1 = bx0 + t->w; by1 = by0 + t->h; }
		int x0 = std::max(cl.x0, bx0), y0 = std::max(cl.y0, by0);
		int x1 = std::min(cl.x1, bx1), y1 = std::min(cl.y1, by1);
		if (x0 >= x1 || y0 >= y1) return;
		touch(y0, y1);

		auto q = [](float v) { return (uint32_t)std::lround(std::min(1.0f, std::max(0.0f, v)) * 255); };
		uint32_t tr = q(tint.r), tg = q(tint.g), tb = q(tint.b), ta = q(tint.a);
		bool plain = tr == 255 && tg == 255 && tb == 255 && ta == 255;
		uint32_t tintWord = premul(tint);	// what an alpha texture's coverage scales

		if (exact)
		{
			// walk the row's runs over the visible columns [cx0, cx1) of the texture
			int cx0 = x0 - bx0, cx1 = x1 - bx0;
			bool solidTint = (tintWord >> 24) == 255;
			for (int yy = y0; yy < y1; yy++)
			{
				uint32_t* d = &scr[cur][(size_t)yy * W + bx0];
				int sy = yy - by0;
				int x = 0;
				for (uint32_t r = t->rowAt[sy]; r < t->rowAt[sy + 1] && x < cx1; r++)
				{
					int len = (int)(t->runs[r] & 0x3fffffffu), kind = (int)(t->runs[r] >> 30);
					int a = std::max(x, cx0), e = std::min(x + len, cx1);
					x += len;
					if (a >= e || kind == SoftTex::CLEAR) continue;
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
						else if (plain) for (int i = a; i < e; i++) d[i] = over(s[i], d[i]);
						else for (int i = a; i < e; i++) d[i] = over(tinted(s[i], tr, tg, tb, ta), d[i]);
					}
				}
			}
			return;
		}

		// scaled: 16.16 source steps from pixel centres
		int64_t stepX = (int64_t)((double)t->w / w * 65536.0), stepY = (int64_t)((double)t->h / h * 65536.0);
		for (int yy = y0; yy < y1; yy++)
		{
			uint32_t* d = &scr[cur][(size_t)yy * W];
			int64_t sv = (int64_t)(((yy + 0.5) - y) * stepY) - (t->smooth ? 32768 : 0);
			int64_t su0 = (int64_t)(((x0 + 0.5) - x) * stepX) - (t->smooth ? 32768 : 0);
			if (!t->smooth)
			{
				int sy = std::min(std::max((int)(sv >> 16), 0), t->h - 1);
				int64_t su = su0;
				for (int xx = x0; xx < x1; xx++, su += stepX)
				{
					int sx = std::min(std::max((int)(su >> 16), 0), t->w - 1);
					uint32_t s;
					if (t->alpha) { uint8_t c = t->a8[(size_t)sy * t->w + sx]; if (!c) continue; s = mul255(tintWord, c); }
					else { s = t->px[(size_t)sy * t->w + sx]; if (!plain) s = tinted(s, tr, tg, tb, ta); }
					d[xx] = over(s, d[xx]);
				}
				continue;
			}
			int syA = (int)(sv >> 16); uint32_t fy = (uint32_t)((sv >> 8) & 255);
			if (sv < 0) { syA = 0; fy = 0; }
			int syB = std::min(syA + 1, t->h - 1);
			syA = std::min(syA, t->h - 1);
			int64_t su = su0;
			for (int xx = x0; xx < x1; xx++, su += stepX)
			{
				int sxA = (int)(su >> 16); uint32_t fx = (uint32_t)((su >> 8) & 255);
				if (su < 0) { sxA = 0; fx = 0; }
				int sxB = std::min(sxA + 1, t->w - 1);
				sxA = std::min(sxA, t->w - 1);
				uint32_t s;
				if (t->alpha)
				{
					uint32_t a = t->a8[(size_t)syA * t->w + sxA], b = t->a8[(size_t)syA * t->w + sxB];
					uint32_t c = t->a8[(size_t)syB * t->w + sxA], e = t->a8[(size_t)syB * t->w + sxB];
					uint32_t top = a * (256 - fx) + b * fx, bot = c * (256 - fx) + e * fx;
					uint32_t cov = (top * (256 - fy) + bot * fy) >> 16;
					if (!cov) continue;
					s = mul255(tintWord, cov);
				}
				else
				{
					const uint32_t* r0 = &t->px[(size_t)syA * t->w];
					const uint32_t* r1 = &t->px[(size_t)syB * t->w];
					uint32_t wa = (256 - fx) * (256 - fy), wb = fx * (256 - fy), wc = (256 - fx) * fy, wd = fx * fy;
					uint32_t pa = r0[sxA], pb = r0[sxB], pc = r1[sxA], pd = r1[sxB];
					s = 0;
					for (int sh = 0; sh < 32; sh += 8)
					{
						uint32_t v = (((pa >> sh) & 255) * wa + ((pb >> sh) & 255) * wb + ((pc >> sh) & 255) * wc + ((pd >> sh) & 255) * wd + 32768) >> 16;
						s |= std::min(v, 255u) << sh;
					}
					if (!plain) s = tinted(s, tr, tg, tb, ta);
				}
				d[xx] = over(s, d[xx]);
			}
		}
	}
}
