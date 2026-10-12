#include "RndsRaster.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_ADVANCES_H
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>

namespace rnds
{
	static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
	static inline int roundi(float v) { return (int)std::floor(v + 0.5f); } // SkScalarRoundToInt

	uint32_t Color::rgba8() const
	{
		auto c = [](float v) { return (uint32_t)roundi(clamp01(v) * 255.0f); };
		return (c(r) << 24) | (c(g) << 16) | (c(b) << 8) | c(a);
	}

	Color mix(const Color& a, float p, const Color& b)
	{
		// color-mix() mixes premultiplied, then divides by the mixed alpha
		float q = 1 - p;
		float al = a.a * p + b.a * q;
		if (al <= 0)
			return Color(0, 0, 0, 0);
		return Color((a.r * a.a * p + b.r * b.a * q) / al, (a.g * a.a * p + b.g * b.a * q) / al, (a.b * a.a * p + b.b * b.a * q) / al, al);
	}

	// ---- Surface ------------------------------------------------------------------------------------------------------

	Surface::Surface(int w_, int h_) : w(w_ > 0 ? w_ : 0), h(h_ > 0 ? h_ : 0), d((size_t)(w_ > 0 ? w_ : 0) * (h_ > 0 ? h_ : 0) * 4, 0.0f) {}

	template<typename F> void Surface::paint(int x0, int y0, int x1, int y1, const Color& c, F coverage)
	{
		paintFrame(x0, y0, x1, y1, 0, 0, 0, 0, c, coverage);
	}

	// like paint(), but skips [sx0,sx1) x [sy0,sy1), where the caller knows the coverage is 0
	template<typename F> void Surface::paintFrame(int x0, int y0, int x1, int y1, int sx0, int sy0, int sx1, int sy1, const Color& c, F coverage)
	{
		if (c.a <= 0)
			return;
		x0 = std::max(x0, 0); y0 = std::max(y0, 0);
		x1 = std::min(x1, w); y1 = std::min(y1, h);
		for (int y = y0; y < y1; y++)
		{
			float* row = &d[((size_t)y * w) * 4];
			bool skipRow = y >= sy0 && y < sy1 && sx1 > sx0;
			for (int x = x0; x < x1; x++)
			{
				if (skipRow && x >= sx0 && x < sx1) { x = sx1 - 1; continue; }
				float cov = coverage(x + 0.5f, y + 0.5f);
				if (cov <= 0)
					continue;
				float a = c.a * (cov > 1 ? 1 : cov);
				float* p = row + x * 4;
				float k = 1 - a;
				p[0] = c.r * a + p[0] * k;
				p[1] = c.g * a + p[1] * k;
				p[2] = c.b * a + p[2] * k;
				p[3] = a + p[3] * k;
			}
		}
	}

	void Surface::drawBitmap(const Bitmap& b, float dx, float dy, const Color& tint)
	{
		int ox = roundi(dx), oy = roundi(dy);
		for (int y = 0; y < b.h; y++)
		{
			int ty = y + oy;
			if (ty < 0 || ty >= h)
				continue;
			for (int x = 0; x < b.w; x++)
			{
				int tx = x + ox;
				if (tx < 0 || tx >= w)
					continue;
				float sr, sg, sb, sa;
				if (b.alpha)
				{
					sa = b.px[(size_t)y * b.w + x] / 255.0f * tint.a;
					sr = tint.r; sg = tint.g; sb = tint.b;
				}
				else
				{
					const uint8_t* s = &b.px[((size_t)y * b.w + x) * 4];
					sa = s[3] / 255.0f * tint.a;
					sr = s[0] / 255.0f * tint.r; sg = s[1] / 255.0f * tint.g; sb = s[2] / 255.0f * tint.b;
				}
				if (sa <= 0)
					continue;
				float* p = &d[((size_t)ty * w + tx) * 4];
				float k = 1 - sa;
				p[0] = sr * sa + p[0] * k;
				p[1] = sg * sa + p[1] * k;
				p[2] = sb * sa + p[2] * k;
				p[3] = sa + p[3] * k;
			}
		}
	}

	Bitmap Surface::toBitmap(bool bleed) const
	{
		Bitmap b;
		b.w = w; b.h = h;
		b.px.assign((size_t)w * h * 4, 0);
		for (size_t i = 0; i < (size_t)w * h; i++)
		{
			const float* p = &d[i * 4];
			float a = p[3];
			uint8_t* o = &b.px[i * 4];
			if (a <= 0.0005f)
				continue;
			o[0] = (uint8_t)roundi(clamp01(p[0] / a) * 255);
			o[1] = (uint8_t)roundi(clamp01(p[1] / a) * 255);
			o[2] = (uint8_t)roundi(clamp01(p[2] / a) * 255);
			o[3] = (uint8_t)roundi(clamp01(a) * 255);
		}
		if (bleed)
		{
			// transparent pixels take a neighbour's colour, so linear filtering doesn't pull black into edges
			std::vector<uint8_t> src = b.px;
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w; x++)
				{
					uint8_t* o = &b.px[((size_t)y * w + x) * 4];
					if (o[3] != 0)
						continue;
					for (int k = 0; k < 8; k++)
					{
						static const int dx[8] = { -1, 1, 0, 0, -1, 1, -1, 1 }, dy[8] = { 0, 0, -1, 1, -1, -1, 1, 1 };
						int nx = x + dx[k], ny = y + dy[k];
						if (nx < 0 || ny < 0 || nx >= w || ny >= h)
							continue;
						const uint8_t* s = &src[((size_t)ny * w + nx) * 4];
						if (s[3] != 0) { o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; break; }
					}
				}
		}
		return b;
	}

	// ---- shapes -------------------------------------------------------------------------------------------------------

	float RRect::coverage(float px, float py) const
	{
		if (w <= 0 || h <= 0)
			return 0;
		if (px < x - 0.5f || py < y - 0.5f || px > x + w + 0.5f || py > y + h + 0.5f)
			return 0;
		float hw = w * 0.5f, hh = h * 0.5f;
		float rr = std::min(r, std::min(hw, hh));
		float qx = std::fabs(px - (x + hw)) - hw + rr;
		float qy = std::fabs(py - (y + hh)) - hh + rr;
		if (rr > 0 && qx <= 0 && qy <= 0 && std::max(qx, qy) - rr <= -0.5f)
			return 1;	// well inside
		float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
		float dist = (ox > 0 && oy > 0 ? std::sqrt(ox * ox + oy * oy) : ox + oy) + std::min(std::max(qx, qy), 0.0f) - rr;
		if (rr <= 0)
		{
			// a plain rectangle: exact area coverage of the pixel
			float cx = std::min(px + 0.5f, x + w) - std::max(px - 0.5f, x);
			float cy = std::min(py + 0.5f, y + h) - std::max(py - 0.5f, y);
			return clamp01(cx) * clamp01(cy);
		}
		return clamp01(0.5f - dist);
	}

	// 1-D overlap of the pixel [p-0.5, p+0.5) with [a, b)
	static inline float overlap(float p, float a, float b) { return clamp01(std::min(p + 0.5f, b) - std::max(p - 0.5f, a)); }

	PaintedBox paintBox(const BoxStyle& st, int w, int h, bool bleed)
	{
		PaintedBox out;
		float minX = 0, minY = 0, maxX = (float)w, maxY = (float)h;
		for (auto& s : st.shadows)
		{
			if (s.inset)
				continue;
			minX = std::min(minX, s.dx - s.spread); minY = std::min(minY, s.dy - s.spread);
			maxX = std::max(maxX, w + s.dx + s.spread); maxY = std::max(maxY, h + s.dy + s.spread);
		}
		int ox = (int)std::ceil(-minX), oy = (int)std::ceil(-minY);
		int W = (int)std::ceil(maxX) + ox, H = (int)std::ceil(maxY) + oy;
		Surface s(W, H);

		RRect B(ox, oy, w, h, st.radius);
		RRect P = B.inset(st.border);
		// the part of the box that no corner reaches: coverage 1 there for B and P
		float core = std::max(st.radius, 0.0f) + st.border + 2;
		int cx0 = (int)std::floor(ox + core), cy0 = (int)std::floor(oy + core), cx1 = (int)std::ceil(ox + w - core), cy1 = (int)std::ceil(oy + h - core);

		// outer shadows, bottom-most first, clipped out of the border box
		for (int i = (int)st.shadows.size() - 1; i >= 0; i--)
		{
			const Shadow& sh = st.shadows[i];
			if (sh.inset)
				continue;
			RRect S(B.x + sh.dx - sh.spread, B.y + sh.dy - sh.spread, B.w + 2 * sh.spread, B.h + 2 * sh.spread, B.r > 0 ? B.r + sh.spread : 0);
			s.paintFrame((int)std::floor(S.x), (int)std::floor(S.y), (int)std::ceil(S.x + S.w), (int)std::ceil(S.y + S.h), cx0, cy0, cx1, cy1, sh.c,
				[&](float x, float y) { return S.coverage(x, y) * (1 - B.coverage(x, y)); });
		}

		// background colour (clip: border box)
		// (in the core B's coverage is exactly 1: no corner, no edge to work out)
		auto inCore = [&](float x, float y) { return x > cx0 && x < cx1 && y > cy0 && y < cy1; };
		s.paint(ox, oy, ox + w, oy + h, st.bg, [&](float x, float y) { return inCore(x, y) ? 1.0f : B.coverage(x, y); });

		// gradient bands (origin: padding box)
		for (auto& band : st.bands)
		{
			float y0 = P.y + band.y0, y1 = band.y1 < 0 ? P.y + P.h : P.y + band.y1;
			s.paint(ox, (int)std::floor(y0), ox + w, (int)std::ceil(y1), band.c,
				[&](float x, float y) { return (inCore(x, y) ? 1.0f : B.coverage(x, y)) * overlap(y, y0, y1); });
		}

		// the dither tile: 4x4 CSS px, white 14% at (0,0) and 7% at (2,2), 1x1 each. The tile is an SVG with
		// shape-rendering: crispEdges, so a device pixel is lit when its centre is inside a dot (no anti-aliasing),
		// with the tile scaled exactly (6.4 device px at 1.6x).
		if (st.dither)
		{
			float sc = st.ditherScale;
			auto inDot = [&](float p, float origin, float off) -> bool
			{
				float u = std::fmod((p - origin) / sc - off, 4.0f);
				if (u < 0) u += 4;
				return u < 1;
			};
			std::vector<uint8_t> colA(W), colB(W), rowA(H), rowB(H);
			for (int x = 0; x < W; x++) { colA[x] = inDot(x + 0.5f, P.x, 0); colB[x] = inDot(x + 0.5f, P.x, 2); }
			for (int y = 0; y < H; y++) { rowA[y] = inDot(y + 0.5f, P.y, 0); rowB[y] = inDot(y + 0.5f, P.y, 2); }
			const Color& w14 = st.ditherA; const Color& w07 = st.ditherB;
			for (int y = oy; y < oy + h && y < H; y++)
			{
				if (!rowA[y] && !rowB[y]) continue;
				for (int x = ox; x < ox + w && x < W; x++)
				{
					const Color* c = rowA[y] && colA[x] ? &w14 : (rowB[y] && colB[x] ? &w07 : nullptr);
					if (!c) continue;
					float cov = (x >= cx0 && x < cx1 && y >= cy0 && y < cy1) ? 1.0f : B.coverage(x + 0.5f, y + 0.5f);
					if (cov <= 0) continue;
					float a = c->a * cov;
					float* p = &s.d[((size_t)y * W + x) * 4];
					float k = 1 - a;
					p[0] = c->r * a + p[0] * k; p[1] = c->g * a + p[1] * k; p[2] = c->b * a + p[2] * k; p[3] = a + p[3] * k;
				}
			}
		}

		// inset shadows, bottom-most first: the padding box minus the shifted, shrunk hole (only near the edges)
		for (int i = (int)st.shadows.size() - 1; i >= 0; i--)
		{
			const Shadow& sh = st.shadows[i];
			if (!sh.inset)
				continue;
			RRect hole(P.x + sh.dx + sh.spread, P.y + sh.dy + sh.spread, P.w - 2 * sh.spread, P.h - 2 * sh.spread, std::max(0.0f, P.r - sh.spread));
			float reach = core + std::fabs(sh.dx) + std::fabs(sh.dy) + sh.spread;
			int hx0 = (int)std::floor(ox + reach), hy0 = (int)std::floor(oy + reach), hx1 = (int)std::ceil(ox + w - reach), hy1 = (int)std::ceil(oy + h - reach);
			s.paintFrame(ox, oy, ox + w, oy + h, hx0, hy0, hx1, hy1, sh.c, [&](float x, float y) { return P.coverage(x, y) * (1 - hole.coverage(x, y)); });
		}

		// border
		if (st.border > 0)
			s.paintFrame(ox, oy, ox + w, oy + h, cx0, cy0, cx1, cy1, st.borderColor, [&](float x, float y) { return B.coverage(x, y) * (1 - P.coverage(x, y)); });

		out.bmp = s.toBitmap(bleed);
		out.originX = ox;
		out.originY = oy;
		return out;
	}

	Bitmap paintMask(int w, int h, float radius)
	{
		Bitmap b;
		b.w = w; b.h = h; b.alpha = true;
		b.px.assign((size_t)w * h, 0);
		RRect r(0, 0, (float)w, (float)h, radius);
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
				b.px[(size_t)y * w + x] = (uint8_t)roundi(r.coverage(x + 0.5f, y + 0.5f) * 255);
		return b;
	}

	Bitmap paintTriangle(float x0, float y0, float x1, float y1, float x2, float y2, const Color& c, int& outX, int& outY)
	{
		int bx0 = (int)std::floor(std::min(x0, std::min(x1, x2))), by0 = (int)std::floor(std::min(y0, std::min(y1, y2)));
		int bx1 = (int)std::ceil(std::max(x0, std::max(x1, x2))), by1 = (int)std::ceil(std::max(y0, std::max(y1, y2)));
		Surface s(bx1 - bx0, by1 - by0);
		auto edge = [](float ax, float ay, float bx, float by, float px, float py) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); };
		float area = edge(x0, y0, x1, y1, x2, y2);
		const int N = 4;
		s.paint(0, 0, s.w, s.h, c, [&](float px, float py) -> float
		{
			int in = 0;
			for (int j = 0; j < N; j++)
				for (int i = 0; i < N; i++)
				{
					float sx = bx0 + px - 0.5f + (i + 0.5f) / N, sy = by0 + py - 0.5f + (j + 0.5f) / N;
					float e0 = edge(x0, y0, x1, y1, sx, sy), e1 = edge(x1, y1, x2, y2, sx, sy), e2 = edge(x2, y2, x0, y0, sx, sy);
					if (area > 0 ? (e0 >= 0 && e1 >= 0 && e2 >= 0) : (e0 <= 0 && e1 <= 0 && e2 <= 0))
						in++;
				}
			return in / (float)(N * N);
		});
		outX = bx0; outY = by0;
		return s.toBitmap();
	}

	// ---- text ---------------------------------------------------------------------------------------------------------

	static FT_Library sFt = nullptr;
	static std::mutex sFtLock;

	std::shared_ptr<Font> Font::load(const std::string& path)
	{
		std::lock_guard<std::mutex> lock(sFtLock);
		if (!sFt && FT_Init_FreeType(&sFt) != 0)
			return nullptr;
		FILE* f = fopen(path.c_str(), "rb");
		if (!f)
			return nullptr;
		std::shared_ptr<Font> font(new Font());
		fseek(f, 0, SEEK_END);
		long n = ftell(f);
		fseek(f, 0, SEEK_SET);
		font->mData.resize(n > 0 ? n : 0);
		size_t got = n > 0 ? fread(font->mData.data(), 1, n, f) : 0;
		fclose(f);
		if ((long)got != n || FT_New_Memory_Face(sFt, font->mData.data(), (FT_Long)n, 0, &font->mFace) != 0)
			return nullptr;
		FT_Select_Charmap(font->mFace, FT_ENCODING_UNICODE);
		font->mUpm = font->mFace->units_per_EM ? font->mFace->units_per_EM : 1000;
		font->mAsc = font->mFace->ascender;
		font->mDesc = -font->mFace->descender;
		return font;
	}

	Font::~Font()
	{
		std::lock_guard<std::mutex> lock(sFtLock);
		if (mFace)
			FT_Done_Face(mFace);
	}

	int Font::upm() const { return mUpm; }
	float Font::ascent(float size) const { return (float)roundi(mAsc * size / mUpm); }
	float Font::descent(float size) const { return (float)roundi(mDesc * size / mUpm); }

	unsigned Font::glyphIndex(uint32_t cp)
	{
		auto it = mGlyphIndex.find(cp);
		if (it != mGlyphIndex.end())
			return it->second;
		unsigned gi = FT_Get_Char_Index(mFace, cp);
		mGlyphIndex[cp] = gi;
		return gi;
	}

	bool Font::hasGlyph(uint32_t cp)
	{
		std::lock_guard<std::mutex> lock(sFtLock);
		return glyphIndex(cp) != 0;
	}

	int Font::advance(uint32_t cp, uint32_t next, float size)
	{
		std::lock_guard<std::mutex> lock(sFtLock);
		unsigned gi = glyphIndex(cp);
		FT_Fixed adv = 0;
		FT_Get_Advance(mFace, gi, FT_LOAD_NO_SCALE, &adv);
		int a = roundi(adv * size / mUpm);
		if (next && FT_HAS_KERNING(mFace))
		{
			FT_Vector k = { 0, 0 };
			if (FT_Get_Kerning(mFace, gi, glyphIndex(next), FT_KERNING_UNSCALED, &k) == 0 && k.x != 0)
				a += roundi(k.x * size / mUpm);
		}
		return a;
	}

	std::u32string utf8to32(const std::string& s)
	{
		std::u32string out;
		out.reserve(s.size());
		for (size_t i = 0; i < s.size();)
		{
			unsigned char c = s[i];
			uint32_t cp;
			int n;
			if (c < 0x80) { cp = c; n = 1; }
			else if ((c >> 5) == 6) { cp = c & 0x1f; n = 2; }
			else if ((c >> 4) == 14) { cp = c & 0x0f; n = 3; }
			else if ((c >> 3) == 30) { cp = c & 0x07; n = 4; }
			else { i++; continue; }
			if (i + n > s.size())
				break;
			for (int k = 1; k < n; k++)
				cp = (cp << 6) | (s[i + k] & 0x3f);
			out.push_back(cp);
			i += n;
		}
		return out;
	}

	std::u32string upper(const std::u32string& s)
	{
		std::u32string o = s;
		for (auto& c : o)
		{
			if (c >= 'a' && c <= 'z') c -= 32;
			else if ((c >= 0xe0 && c <= 0xfe && c != 0xf7)) c -= 32;
			else if (c == 0xff) c = 0x178;
			else if (c >= 0x100 && c <= 0x17f && (c & 1) && c != 0x131 && c != 0x138 && c != 0x149 && !(c >= 0x139 && c <= 0x148) && !(c >= 0x179 && c <= 0x17e)) c -= 1;
			else if (((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17e)) && !(c & 1)) c -= 1;
		}
		return o;
	}

	static std::u32string prepared(const std::u32string& t, const TextStyle& st) { return st.upper ? upper(t) : t; }

	TextLine layoutLine(const std::u32string& text, const TextStyle& st)
	{
		TextLine line;
		line.text = prepared(text, st);
		float pen = 0;
		for (size_t i = 0; i < line.text.size(); i++)
		{
			line.x.push_back(pen);
			uint32_t next = i + 1 < line.text.size() ? line.text[i + 1] : 0;
			pen += st.font->advance(line.text[i], next, st.size) + st.letterSpacing;
		}
		line.width = pen;
		return line;
	}

	TextLine layoutEllipsis(const std::u32string& text, const TextStyle& st, int maxWidth)
	{
		TextLine full = layoutLine(text, st);
		if (full.width <= maxWidth + 0.01f)
			return full;
		const std::u32string ell = U"…";
		float ellW = st.font->advance(ell[0], 0, st.size) + st.letterSpacing;
		// keep the characters that end before maxWidth - ellipsis (the last kept one without its kerning to the next)
		size_t keep = 0;
		for (size_t i = 0; i < full.text.size(); i++)
		{
			float end = full.x[i] + st.font->advance(full.text[i], 0, st.size) + st.letterSpacing;
			if (end + ellW > maxWidth + 0.01f)
				break;
			keep = i + 1;
		}
		return layoutLine(full.text.substr(0, keep) + ell, TextStyle{ st.font, st.size, st.letterSpacing, false });
	}

	std::vector<TextLine> layoutWrap(const std::u32string& text0, const TextStyle& st, int maxWidth, int maxLines, int firstLineIndent)
	{
		std::u32string text = prepared(text0, st);
		TextStyle plain = st;
		plain.upper = false;

		std::vector<std::u32string> words;
		std::u32string cur;
		for (auto c : text)
		{
			if (c == U' ') { words.push_back(cur); cur.clear(); }
			else cur.push_back(c);
		}
		words.push_back(cur);

		std::vector<std::u32string> lines;
		std::u32string line;
		size_t w = 0;
		std::vector<size_t> lineStartWord;
		for (; w < words.size(); w++)
		{
			std::u32string trial = line.empty() ? words[w] : line + U" " + words[w];
			int indent = lines.empty() ? firstLineIndent : 0;
			if (!line.empty() && layoutLine(trial, plain).width + indent > maxWidth + 0.01f)
			{
				lines.push_back(line);
				line = words[w];
			}
			else
				line = trial;
		}
		lines.push_back(line);

		std::vector<TextLine> out;
		for (size_t i = 0; i < lines.size(); i++)
		{
			if (maxLines > 0 && (int)i == maxLines - 1 && lines.size() > (size_t)maxLines)
			{
				// -webkit-line-clamp: the last shown line ends in an ellipsis
				TextLine l = layoutLine(lines[i], plain);
				const std::u32string ell = U"…";
				float ellW = st.font->advance(ell[0], 0, st.size) + st.letterSpacing;
				int indent = i == 0 ? firstLineIndent : 0;
				if (l.width + ellW + indent <= maxWidth + 0.01f)
					out.push_back(layoutLine(lines[i] + ell, plain));
				else
					out.push_back(layoutEllipsis(lines[i] + U"……", plain, maxWidth - indent));
				break;
			}
			out.push_back(layoutLine(lines[i], plain));
		}
		return out;
	}

	int gTextMode = 2;	// no hinting, whole-pixel glyph origins: what matched Chromium best (1x and 1.6x)

	TextBitmap renderLine(const TextLine& line, const TextStyle& st, float scale, float baselineCss, float leftFrac, float topFrac)
	{
		// leftFrac/topFrac: the fractional part (device px) of the line box's position, which the caller dropped when it
		// rounded the bitmap's placement; glyph origins are computed from the exact position and then rounded, like Skia
		// does without subpixel positioning.
		TextBitmap tb;
		std::lock_guard<std::mutex> lock(sFtLock);
		FT_Face face = st.font->mFace;
		float size = st.size * scale;
		FT_Set_Char_Size(face, 0, (FT_F26Dot6)roundi(size * 64), 72, 72);

		float asc = st.font->mAsc * size / st.font->mUpm, desc = st.font->mDesc * size / st.font->mUpm;
		int pad = (int)std::ceil(size * 0.3f) + 2;
		tb.padX = pad;
		tb.padY = pad;
		Bitmap& b = tb.bmp;
		b.alpha = true;
		b.w = (int)std::ceil(line.width * scale) + 2 * pad;
		b.h = (int)std::ceil(std::max(baselineCss * scale + desc, asc + desc)) + 2 * pad;
		b.px.assign((size_t)b.w * b.h, 0);

		float baseline = pad + topFrac + baselineCss * scale;
		bool subpixel = (gTextMode & 1) != 0;
		FT_Int32 flags = (gTextMode & 2) ? (FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) : FT_LOAD_TARGET_LIGHT;
		for (size_t i = 0; i < line.text.size(); i++)
		{
			unsigned gi = st.font->glyphIndex(line.text[i]);
			if (FT_Load_Glyph(face, gi, flags) != 0)
				continue;
			float penX = pad + leftFrac + line.x[i] * scale;
			int ix = subpixel ? (int)std::floor(penX) : roundi(penX);
			int iy = roundi(baseline);
			if (subpixel && face->glyph->format == FT_GLYPH_FORMAT_OUTLINE)
				FT_Outline_Translate(&face->glyph->outline, (FT_Pos)roundi((penX - ix) * 64), 0);
			if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0)
				continue;
			FT_GlyphSlot g = face->glyph;
			int gx = ix + g->bitmap_left;
			int gy = iy - g->bitmap_top;
			for (unsigned r = 0; r < g->bitmap.rows; r++)
			{
				int ty = gy + (int)r;
				if (ty < 0 || ty >= b.h)
					continue;
				const unsigned char* src = g->bitmap.buffer + r * g->bitmap.pitch;
				for (unsigned c = 0; c < g->bitmap.width; c++)
				{
					int tx = gx + (int)c;
					if (tx < 0 || tx >= b.w || !src[c])
						continue;
					uint8_t& d = b.px[(size_t)ty * b.w + tx];
					int v = d + src[c] - d * src[c] / 255;
					d = (uint8_t)(v > 255 ? 255 : v);
				}
			}
		}
		return tb;
	}

	// ---- images -------------------------------------------------------------------------------------------------------

	static Bitmap(*sDecoder)(const std::string&) = nullptr;
	void setDecoder(Bitmap(*decoder)(const std::string& path)) { sDecoder = decoder; }
	Bitmap decodeFile(const std::string& path) { return sDecoder && !path.empty() ? sDecoder(path) : Bitmap(); }

	Bitmap resize(const Bitmap& src, int w, int h)
	{
		Bitmap out;
		if (src.empty() || w <= 0 || h <= 0)
			return out;
		out.w = w; out.h = h; out.alpha = src.alpha;
		int ch = src.alpha ? 1 : 4;
		out.px.assign((size_t)w * h * ch, 0);
		float sx = src.w / (float)w, sy = src.h / (float)h;
		if (sx <= 1 && sy <= 1)
		{
			// bilinear (premultiplied)
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w; x++)
				{
					float fx = (x + 0.5f) * sx - 0.5f, fy = (y + 0.5f) * sy - 0.5f;
					int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
					float tx = fx - x0, ty = fy - y0;
					float acc[4] = { 0, 0, 0, 0 };
					for (int j = 0; j < 2; j++)
						for (int i = 0; i < 2; i++)
						{
							int xx = std::min(std::max(x0 + i, 0), src.w - 1), yy = std::min(std::max(y0 + j, 0), src.h - 1);
							float wgt = (i ? tx : 1 - tx) * (j ? ty : 1 - ty);
							const uint8_t* p = &src.px[((size_t)yy * src.w + xx) * ch];
							if (ch == 1) acc[3] += p[0] * wgt;
							else
							{
								float a = p[3] / 255.0f;
								acc[0] += p[0] * a * wgt; acc[1] += p[1] * a * wgt; acc[2] += p[2] * a * wgt; acc[3] += p[3] * wgt;
							}
						}
					uint8_t* o = &out.px[((size_t)y * w + x) * ch];
					if (ch == 1) o[0] = (uint8_t)roundi(acc[3]);
					else
					{
						float a = acc[3] / 255.0f;
						o[3] = (uint8_t)roundi(acc[3]);
						if (a > 0) { o[0] = (uint8_t)roundi(std::min(255.0f, acc[0] / a)); o[1] = (uint8_t)roundi(std::min(255.0f, acc[1] / a)); o[2] = (uint8_t)roundi(std::min(255.0f, acc[2] / a)); }
					}
				}
			return out;
		}
		// area average (premultiplied) when shrinking
		for (int y = 0; y < h; y++)
		{
			float y0 = y * sy, y1 = (y + 1) * sy;
			for (int x = 0; x < w; x++)
			{
				float x0 = x * sx, x1 = (x + 1) * sx;
				float acc[4] = { 0, 0, 0, 0 }, tot = 0;
				for (int yy = (int)std::floor(y0); yy < (int)std::ceil(y1) && yy < src.h; yy++)
				{
					float wy = std::min(y1, yy + 1.0f) - std::max(y0, (float)yy);
					if (wy <= 0) continue;
					for (int xx = (int)std::floor(x0); xx < (int)std::ceil(x1) && xx < src.w; xx++)
					{
						float wx = std::min(x1, xx + 1.0f) - std::max(x0, (float)xx);
						if (wx <= 0) continue;
						float wgt = wx * wy;
						const uint8_t* p = &src.px[((size_t)yy * src.w + xx) * ch];
						if (ch == 1) acc[3] += p[0] * wgt;
						else
						{
							float a = p[3] / 255.0f;
							acc[0] += p[0] * a * wgt; acc[1] += p[1] * a * wgt; acc[2] += p[2] * a * wgt; acc[3] += p[3] * wgt;
						}
						tot += wgt;
					}
				}
				if (tot <= 0) continue;
				uint8_t* o = &out.px[((size_t)y * w + x) * ch];
				if (ch == 1) o[0] = (uint8_t)roundi(acc[3] / tot);
				else
				{
					float a = acc[3] / tot / 255.0f;
					o[3] = (uint8_t)roundi(acc[3] / tot);
					if (a > 0) { o[0] = (uint8_t)roundi(std::min(255.0f, acc[0] / tot / a)); o[1] = (uint8_t)roundi(std::min(255.0f, acc[1] / tot / a)); o[2] = (uint8_t)roundi(std::min(255.0f, acc[2] / tot / a)); }
				}
			}
		}
		return out;
	}

	Bitmap resizeNearest(const Bitmap& src, int w, int h)
	{
		Bitmap out;
		if (src.empty() || w <= 0 || h <= 0)
			return out;
		out.w = w; out.h = h; out.alpha = src.alpha;
		int ch = src.alpha ? 1 : 4;
		out.px.resize((size_t)w * h * ch);
		for (int y = 0; y < h; y++)
		{
			int sy = std::min(src.h - 1, (int)((y + 0.5f) * src.h / h));
			for (int x = 0; x < w; x++)
			{
				int sx = std::min(src.w - 1, (int)((x + 0.5f) * src.w / w));
				memcpy(&out.px[((size_t)y * w + x) * ch], &src.px[((size_t)sy * src.w + sx) * ch], ch);
			}
		}
		return out;
	}

	Bitmap cover(const Bitmap& src, int w, int h)
	{
		if (src.empty() || w <= 0 || h <= 0)
			return Bitmap();
		float ia = src.w / (float)src.h, ra = w / (float)h;
		float dw, dh;
		if (ia > ra) { dh = (float)h; dw = h * ia; }
		else { dw = (float)w; dh = w / ia; }
		// crop the source to the visible part, then resample to w x h
		float cw = src.w * (w / dw), chh = src.h * (h / dh);
		int cx = (int)std::floor((src.w - cw) / 2), cy = (int)std::floor((src.h - chh) / 2);
		int icw = std::max(1, (int)std::ceil(cw)), ich = std::max(1, (int)std::ceil(chh));
		icw = std::min(icw, src.w - cx); ich = std::min(ich, src.h - cy);
		Bitmap crop;
		crop.w = icw; crop.h = ich; crop.alpha = src.alpha;
		int ch = src.alpha ? 1 : 4;
		crop.px.resize((size_t)icw * ich * ch);
		for (int y = 0; y < ich; y++)
			memcpy(&crop.px[(size_t)y * icw * ch], &src.px[((size_t)(y + cy) * src.w + cx) * ch], (size_t)icw * ch);
		return resize(crop, w, h);
	}

	Bitmap crop(const Bitmap& src, int x, int y, int w, int h)
	{
		if (src.empty() || src.alpha) return src;
		x = std::max(0, std::min(x, src.w - 1)); y = std::max(0, std::min(y, src.h - 1));
		w = std::max(1, std::min(w, src.w - x)); h = std::max(1, std::min(h, src.h - y));
		Bitmap r;
		r.w = w; r.h = h;
		r.px.resize((size_t)w * h * 4);
		for (int yy = 0; yy < h; yy++)
			memcpy(&r.px[(size_t)yy * w * 4], &src.px[((size_t)(yy + y) * src.w + x) * 4], (size_t)w * 4);
		return r;
	}

	static bool nearWhite(const uint8_t* p)
	{
		int mx = std::max(p[0], std::max(p[1], p[2])), mn = std::min(p[0], std::min(p[1], p[2]));
		return mn > 190 && mx - mn < 40 && p[3] > 128;
	}

	// a row or column that is plain: mostly near-white, or one flat colour (a label's white margin, the card's plastic)
	static bool plainLine(const Bitmap& b, int x0, int y0, int dx, int dy, int n)
	{
		int white = 0, cnt = 0, grey = 0;
		double sum = 0, sum2 = 0;
		for (int i = 0; i < n; i += 2, cnt++)
		{
			const uint8_t* p = &b.px[((size_t)(y0 + dy * i) * b.w + (x0 + dx * i)) * 4];
			if (nearWhite(p) || p[3] < 128) white++;
			int mx = std::max(p[0], std::max(p[1], p[2])), mn = std::min(p[0], std::min(p[1], p[2]));
			if (mx - mn < 24) grey++;
			double l = 0.3 * p[0] + 0.59 * p[1] + 0.11 * p[2];
			sum += l; sum2 += l * l;
		}
		if (cnt == 0) return true;
		double m = sum / cnt, sd = std::sqrt(std::max(0.0, sum2 / cnt - m * m));
		// white, or the card's flat grey plastic; a flat coloured band is the art's own
		return white > cnt * 6 / 10 || (sd < 10 && grey > cnt * 8 / 10);
	}

	// trims plain edges (up to a quarter of each side), so the picture inside fills what it is fitted to
	static Bitmap trimPlain(const Bitmap& b)
	{
		if (b.empty() || b.alpha) return b;
		int x0 = 0, y0 = 0, x1 = b.w - 1, y1 = b.h - 1;
		while (x0 < b.w / 4 && plainLine(b, x0, y0, 0, 1, y1 - y0 + 1)) x0++;
		while (x1 > b.w * 3 / 4 && plainLine(b, x1, y0, 0, 1, y1 - y0 + 1)) x1--;
		while (y0 < b.h / 4 && plainLine(b, x0, y0, 1, 0, x1 - x0 + 1)) y0++;
		while (y1 > b.h * 3 / 4 && plainLine(b, x0, y1, 1, 0, x1 - x0 + 1)) y1--;
		// and one more pixel: the edge of the art itself is often a thin line
		x0 = std::min(x0 + 1, x1); y0 = std::min(y0 + 1, y1); x1 = std::max(x1 - 1, x0); y1 = std::max(y1 - 1, y0);
		return crop(b, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
	}

	Bitmap cardLabelArt(const Bitmap& scan)
	{
		if (scan.empty() || scan.alpha) return Bitmap();
		// the card: the bounding box of what isn't transparent (cut-out scans) or the whole picture
		int x0 = scan.w, y0 = scan.h, x1 = -1, y1 = -1;
		for (int y = 0; y < scan.h; y++)
			for (int x = 0; x < scan.w; x++)
				if (scan.px[((size_t)y * scan.w + x) * 4 + 3] > 16)
				{
					x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
				}
		if (x1 < x0 || y1 < y0) return Bitmap();
		int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
		// the label spans the card but for its plastic edges. From the top: plastic, the white NINTENDO DS header, then
		// the art; from the bottom: plastic, the white strip with the game code, then the art. The art is what's
		// between the two white bands.
		int lx0 = x0 + cw * 8 / 100, lx1 = x1 - cw * 8 / 100;
		auto whiteRow = [&](int y)
		{
			int white = 0, n = 0;
			for (int x = lx0; x <= lx1; x += 2, n++)
				if (nearWhite(&scan.px[((size_t)y * scan.w + x) * 4])) white++;
			return n > 0 && white >= n * 45 / 100;
		};
		int ay0 = -1, ay1 = -1;
		{
			int y = y0, lim = y0 + ch * 40 / 100;
			while (y < lim && !whiteRow(y)) y++;			// the plastic above the label
			if (y < lim)
			{
				while (y < lim && whiteRow(y)) y++;		// the header
				if (y < lim) ay0 = y;
			}
		}
		{
			int y = y1, lim = y1 - ch * 40 / 100;
			while (y > lim && !whiteRow(y)) y--;			// the plastic below the label
			if (y > lim)
			{
				while (y > lim && whiteRow(y)) y--;		// the code strip
				if (y > lim) ay1 = y;
			}
		}
		if (ay0 < 0) ay0 = y0 + ch * 18 / 100;		// where the art sits on a DS card's label when the scan doesn't say
		if (ay1 < 0 || ay1 - ay0 < ch * 30 / 100) ay1 = y0 + ch * 82 / 100;
		// all of the label's width: its sides are art too, and a white or checkered background (Mario Kart DS) read as
		// a plain margin, so trimming them zoomed in on the middle. The cartridge's label window (92x86) is narrower
		// than the art band: a quarter of the difference comes from the label above and below the band, so the logo
		// keeps its width, and the rest is cropped from the sides.
		int lw = lx1 - lx0 + 1, ext = std::max(0, (lw * 86 / 92 - (ay1 - ay0 + 1)) / 4);
		ay0 = std::max(y0, ay0 - ext); ay1 = std::min(y1, ay1 + ext);
		return crop(scan, lx0, ay0, lw, ay1 - ay0 + 1);
	}

	Bitmap coverWithoutStrip(const Bitmap& box)
	{
		if (box.empty() || box.alpha) return box;
		// columns from the left that are mostly near-white, at least the 13.5% the strip takes on DS covers
		int x = 0, limit = box.w / 5;
		for (; x < limit; x++)
		{
			int white = 0, n = 0;
			for (int y = 0; y < box.h; y += 2, n++)
				if (nearWhite(&box.px[((size_t)y * box.w + x) * 4])) white++;
			if (white < n * 55 / 100) break;
		}
		if (x <= box.w * 4 / 100) return box;
		x = std::max(x, box.w * 135 / 1000) + 2;
		return trimPlain(crop(box, x, 0, box.w - x, box.h));
	}

	Bitmap coverTop(const Bitmap& src, int w, int h)
	{
		if (src.empty() || w <= 0 || h <= 0)
			return Bitmap();
		float ia = src.w / (float)src.h, ra = w / (float)h;
		if (ia > ra)
			return cover(src, w, h);		// too wide: centred left and right, all of its height
		// too tall: all of its width, from its top (a label's logo is at the top)
		int ch = std::max(1, std::min(src.h, (int)std::lround(src.w / ra)));
		return resize(crop(src, 0, 0, src.w, ch), w, h);
	}

	void containSize(int iw, int ih, int bw, int bh, float& outW, float& outH)
	{
		if (iw <= 0 || ih <= 0) { outW = (float)bw; outH = (float)bh; return; }
		float s = std::min(bw / (float)iw, bh / (float)ih);
		outW = iw * s;
		outH = ih * s;
	}

	void blit(Bitmap& dst, const Bitmap& src, int ox, int oy)
	{
		if (dst.alpha || src.alpha)
			return;
		for (int y = 0; y < src.h; y++)
		{
			int ty = y + oy;
			if (ty < 0 || ty >= dst.h) continue;
			for (int x = 0; x < src.w; x++)
			{
				int tx = x + ox;
				if (tx < 0 || tx >= dst.w) continue;
				const uint8_t* s = &src.px[((size_t)y * src.w + x) * 4];
				uint8_t* d = &dst.px[((size_t)ty * dst.w + tx) * 4];
				float sa = s[3] / 255.0f, da = d[3] / 255.0f;
				float oa = sa + da * (1 - sa);
				if (oa <= 0) { d[0] = d[1] = d[2] = d[3] = 0; continue; }
				for (int k = 0; k < 3; k++)
					d[k] = (uint8_t)roundi((s[k] * sa + d[k] * da * (1 - sa)) / oa);
				d[3] = (uint8_t)roundi(oa * 255);
			}
		}
	}

	// ---- a DS ROM's own icon ------------------------------------------------------------------------------------------

	static ZipReader sZipReader = nullptr;
	void setZipReader(ZipReader reader) { sZipReader = reader; }

	static uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

	static const size_t NDS_BANNER_AT = 0x68;		// the header's u32: where the banner is
	static const size_t NDS_HEADER = 0x200;			// a banner can't start inside the header
	static const size_t NDS_BANNER = 0x240;			// version, CRCs, the icon's tiles and palette
	static const uint64_t ZIP_REACH = 32u << 20;	// how far into a zipped ROM the banner is looked for

	Bitmap ndsBannerIcon(const uint8_t* b)
	{
		Bitmap out;
		unsigned version = b[0] | (b[1] << 8);
		if (version != 1 && version != 2 && version != 3 && version != 0x103)
			return out;
		uint8_t pal[16][4];
		for (int i = 0; i < 16; i++)
		{
			unsigned c = b[0x220 + 2 * i] | (b[0x221 + 2 * i] << 8);	// BGR555
			unsigned r = c & 31, g = (c >> 5) & 31, bl = (c >> 10) & 31;
			pal[i][0] = (uint8_t)((r << 3) | (r >> 2)); pal[i][1] = (uint8_t)((g << 3) | (g >> 2)); pal[i][2] = (uint8_t)((bl << 3) | (bl >> 2));
			pal[i][3] = i == 0 ? 0 : 255;
		}
		pal[0][0] = pal[0][1] = pal[0][2] = 0;
		out.w = out.h = 32;
		out.px.assign(32 * 32 * 4, 0);
		for (int t = 0; t < 16; t++)		// tile t = ty * 4 + tx
			for (int y = 0; y < 8; y++)
				for (int x = 0; x < 8; x++)
				{
					uint8_t v = b[0x20 + t * 32 + y * 4 + x / 2];
					memcpy(&out.px[(((t / 4) * 8 + y) * 32 + (t % 4) * 8 + x) * 4], pal[x & 1 ? v >> 4 : v & 15], 4);
				}
		return out;
	}

	// the header's banner offset, then the banner, from a ROM read in order (a zip member streams)
	struct BannerGrab
	{
		uint64_t size = 0, pos = 0, at = 0;	// the ROM's size; bytes seen; the banner's offset (0 until the header is in)
		uint64_t reach = ~0ull;
		std::vector<uint8_t> head, banner;
		bool failed = false;
		bool done() const { return banner.size() == NDS_BANNER; }
		// false: enough (or nothing to find)
		bool feed(const uint8_t* p, size_t n)
		{
			for (size_t i = 0; i < n && !failed && !done(); )
			{
				uint64_t o = pos + i;
				if (head.size() < NDS_BANNER_AT + 4)
				{
					head.push_back(p[i++]);
					if (head.size() == NDS_BANNER_AT + 4)
					{
						at = le32(&head[NDS_BANNER_AT]);
						failed = at < NDS_HEADER || at + NDS_BANNER > size || at > reach;
					}
					continue;
				}
				if (o < at) { i += (size_t)std::min<uint64_t>(at - o, n - i); continue; }	// skip to the banner
				size_t k = std::min(n - i, NDS_BANNER - banner.size());
				banner.insert(banner.end(), p + i, p + i + k);
				i += k;
			}
			pos += n;
			return !failed && !done();
		}
	};

	static bool endsWithNoCase(const std::string& s, const char* suffix)
	{
		size_t n = strlen(suffix);
		if (s.size() < n) return false;
		for (size_t i = 0; i < n; i++)
			if (tolower((unsigned char)s[s.size() - n + i]) != suffix[i]) return false;
		return true;
	}

	static Bitmap readRomIcon(const std::string& path)
	{
		if (endsWithNoCase(path, ".nds"))
		{
			FILE* f = fopen(path.c_str(), "rb");
			if (!f) return Bitmap();
			uint8_t head[NDS_BANNER_AT + 4], banner[NDS_BANNER];
			bool ok = fread(head, 1, sizeof head, f) == sizeof head && fseek(f, 0, SEEK_END) == 0;
			long size = ok ? ftell(f) : -1;
			uint64_t at = ok ? le32(&head[NDS_BANNER_AT]) : 0;
			ok = ok && size > 0 && at >= NDS_HEADER && at + NDS_BANNER <= (uint64_t)size &&
				fseek(f, (long)at, SEEK_SET) == 0 && fread(banner, 1, sizeof banner, f) == sizeof banner;
			fclose(f);
			return ok ? ndsBannerIcon(banner) : Bitmap();
		}
		if (!endsWithNoCase(path, ".zip") || !sZipReader)
			return Bitmap();
		BannerGrab g;
		g.reach = ZIP_REACH;
		sZipReader(path, [&g](const std::vector<ZipMember>& ms) -> int
		{
			int first = -1;
			for (int i = 0; i < (int)ms.size(); i++)
			{
				if (!ms[i].name.empty() && ms[i].name.back() == '/') continue;	// a folder
				if (endsWithNoCase(ms[i].name, ".nds")) { g.size = ms[i].size; return i; }
				if (first < 0) first = i;
			}
			if (first >= 0) g.size = ms[first].size;
			return first;
		}, [&g](const uint8_t* p, size_t n) { return g.feed(p, n); });
		return g.done() ? ndsBannerIcon(g.banner.data()) : Bitmap();
	}

	Bitmap romIcon(const std::string& path)
	{
		static std::mutex lock;
		static std::map<std::string, Bitmap> memo;	// 4 KB an icon (and nothing for a ROM without one)
		static std::deque<std::string> order;
		{
			std::lock_guard<std::mutex> l(lock);
			auto it = memo.find(path);
			if (it != memo.end()) return it->second;
		}
		Bitmap b = readRomIcon(path);
		std::lock_guard<std::mutex> l(lock);
		if (memo.emplace(path, b).second)
		{
			order.push_back(path);
			if (order.size() > 512) { memo.erase(order.front()); order.pop_front(); }
		}
		return b;
	}
}
