#pragma once
// ROCKNIXDS rnds engine: CPU rasterizer for the pieces the mockup is drawn from. Boxes are painted the way CSS paints a
// box (outer box-shadows, background colour, hard-stop gradient bands and the dither tile, inset box-shadows, border),
// text the way Chrome lays out Pixelify Sans on Linux (whole-pixel glyph advances and kerning, light hinting), images
// are decoded and fitted (cover / contain). Everything here is plain C++ and FreeType, so the same code runs in ES and
// in the host test harness that compares the engine's frames with the mockup rendered by Chromium.
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

typedef struct FT_LibraryRec_* FT_Library;
typedef struct FT_FaceRec_* FT_Face;

namespace rnds
{
	struct Color
	{
		float r = 0, g = 0, b = 0, a = 0; // straight alpha, 0..1
		Color() {}
		Color(float r_, float g_, float b_, float a_ = 1) : r(r_), g(g_), b(b_), a(a_) {}
		static Color hex(uint32_t rgb, float a = 1) { return Color(((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f, a); }
		bool transparent() const { return a <= 0; }
		uint32_t rgba8() const; // 0xRRGGBBAA
	};

	// color-mix(in srgb, a p%, b)
	Color mix(const Color& a, float p, const Color& b);

	// RGBA8 (straight alpha) or A8 pixels, rows top to bottom
	struct Bitmap
	{
		int w = 0, h = 0;
		bool alpha = false;		// A8 coverage (drawn tinted) instead of RGBA8
		std::vector<uint8_t> px;
		bool empty() const { return w <= 0 || h <= 0; }
	};

	// premultiplied float surface everything is composited on before it becomes a Bitmap
	class Surface
	{
	public:
		Surface(int w, int h);
		int w, h;
		std::vector<float> d;	// premultiplied RGBA

		// source-over a colour through a per-pixel coverage function over [x0,x1) x [y0,y1)
		template<typename F> void paint(int x0, int y0, int x1, int y1, const Color& c, F coverage);
		template<typename F> void paintFrame(int x0, int y0, int x1, int y1, int sx0, int sy0, int sx1, int sy1, const Color& c, F coverage);
		void drawBitmap(const Bitmap& b, float dx, float dy, const Color& tint = Color(1, 1, 1, 1));
		Bitmap toBitmap(bool bleed = true) const;
	};

	// ---- shapes ---------------------------------------------------------------------------------------------------
	struct RRect
	{
		float x = 0, y = 0, w = 0, h = 0, r = 0;
		RRect() {}
		RRect(float x_, float y_, float w_, float h_, float r_) : x(x_), y(y_), w(w_), h(h_), r(r_) {}
		RRect inset(float d) const { return RRect(x + d, y + d, w - 2 * d, h - 2 * d, r - d > 0 ? r - d : 0); }
		RRect moved(float dx, float dy) const { return RRect(x + dx, y + dy, w, h, r); }
		float coverage(float px, float py) const; // anti-aliased coverage of the pixel whose centre is (px, py)
	};

	// ---- CSS box ----------------------------------------------------------------------------------------------------
	struct Band { float y0, y1; Color c; };			// background: linear-gradient(...) hard stops, padding-box y
	struct Shadow { float dx, dy, spread; Color c; bool inset; };

	struct BoxStyle
	{
		float radius = 0;
		float border = 0;			// device pixels (already snapped)
		Color borderColor;
		Color bg;
		std::vector<Band> bands;	// device pixels from the padding box's top; y1 < 0 means to the bottom
		bool dither = false;		// the mockup's --dither tile, origin at the padding box
		float ditherScale = 1;		// CSS px -> device px
		Color ditherA = Color(1, 1, 1, 0.14f), ditherB = Color(1, 1, 1, 0.07f);	// the tile's two dots (light themes: dark dots)
		std::vector<Shadow> shadows;	// CSS order: the first one is on top
	};

	// The box at (0,0) w x h in device pixels. The bitmap also holds the outer shadows: the box's top-left corner is at
	// (originX, originY) in it.
	struct PaintedBox { Bitmap bmp; int originX = 0, originY = 0; };
	PaintedBox paintBox(const BoxStyle& st, int w, int h, bool bleed = true);
	// A8 coverage of a rounded rectangle w x h
	Bitmap paintMask(int w, int h, float radius);

	// a triangle (the bubble's pointer), anti-aliased, into its bounding box
	Bitmap paintTriangle(float x0, float y0, float x1, float y1, float x2, float y2, const Color& c, int& outX, int& outY);

	// ---- text -------------------------------------------------------------------------------------------------------
	struct TextLine; struct TextStyle; struct TextBitmap;
	class Font
	{
	public:
		static std::shared_ptr<Font> load(const std::string& path);
		~Font();

		bool hasGlyph(uint32_t cp);
		int upm() const;
		float ascent(float size) const;	// whole pixels, as Chrome rounds them
		float descent(float size) const;
		float normalLineHeight(float size) const { return ascent(size) + descent(size); }

		// whole-pixel advance of cp followed by next (kerning included, each rounded on its own)
		int advance(uint32_t cp, uint32_t next, float size);

	private:
		Font() {}
		friend TextBitmap renderLine(const TextLine& line, const TextStyle& st, float scale, float baselineCss, float leftFrac, float topFrac);
		std::vector<uint8_t> mData;
		FT_Face mFace = nullptr;
		int mUpm = 1000, mAsc = 0, mDesc = 0;
		std::unordered_map<uint32_t, unsigned> mGlyphIndex;
		unsigned glyphIndex(uint32_t cp);
	};

	struct TextStyle
	{
		std::shared_ptr<Font> font;
		float size = 16;			// device pixels
		float letterSpacing = 0;	// device pixels, after every character (CSS letter-spacing)
		bool upper = false;			// text-transform: uppercase
	};

	std::u32string utf8to32(const std::string& s);
	std::u32string upper(const std::u32string& s);

	// laid-out single line: whole-pixel pen positions
	struct TextLine
	{
		std::u32string text;
		std::vector<float> x;	// pen x of each character (whole-pixel advances plus letter-spacing)
		float width = 0;		// advances + letter-spacing (CSS inline size)
	};

	TextLine layoutLine(const std::u32string& text, const TextStyle& st);
	// text-overflow: ellipsis within maxWidth (device px)
	TextLine layoutEllipsis(const std::u32string& text, const TextStyle& st, int maxWidth);
	// greedy wrap at spaces (CSS white-space: normal), at most maxLines (the last one ends in an ellipsis when cut)
	std::vector<TextLine> layoutWrap(const std::u32string& text, const TextStyle& st, int maxWidth, int maxLines = 0, int firstLineIndent = 0);

	// A8 bitmap of a line laid out in CSS px, drawn at scale (device px per CSS px). The bitmap's (0,0) goes at device
	// (round(left * scale) - padX, round(top * scale) - padY) for a line box whose top-left is (left, top) in CSS px;
	// baselineCss is the baseline's offset from the line box top in CSS px.
	struct TextBitmap { Bitmap bmp; int padX = 0, padY = 0; };
	extern int gTextMode;	// calibration switch for the harness (hinting / positioning variants)
	TextBitmap renderLine(const TextLine& line, const TextStyle& st, float scale, float baselineCss, float leftFrac = 0, float topFrac = 0);

	// ---- images -----------------------------------------------------------------------------------------------------
	Bitmap decodeFile(const std::string& path);		// RGBA8; empty on failure (decoder is set by the host)
	void setDecoder(Bitmap(*decoder)(const std::string& path));

	// resample (bilinear, area-averaged when shrinking) to w x h
	Bitmap resize(const Bitmap& src, int w, int h);
	// nearest-neighbour scale (pixel art)
	Bitmap resizeNearest(const Bitmap& src, int w, int h);
	// object-fit: cover, centred, into w x h
	Bitmap cover(const Bitmap& src, int w, int h);
	// cover, but a picture taller than the box keeps its top (cut at the bottom)
	Bitmap coverTop(const Bitmap& src, int w, int h);
	// object-fit: contain: the fitted size (w x h stays the box)
	void containSize(int iw, int ih, int bw, int bh, float& outW, float& outH);
	// draws src over dst at (x, y) (straight alpha both)
	void blit(Bitmap& dst, const Bitmap& src, int x, int y);
	// a part of an RGBA8 bitmap (clamped to it)
	Bitmap crop(const Bitmap& src, int x, int y, int w, int h);
	// the artwork window of a real DS card scan: the label without its white NINTENDO DS header and code footer, and
	// without the card's plastic around it
	Bitmap cardLabelArt(const Bitmap& scan);
	// a DS box cover without the case's white NINTENDO DS strip down its left edge
	Bitmap coverWithoutStrip(const Bitmap& box);

	// ---- a DS ROM's own icon ----------------------------------------------------------------------------------------
	// The 32x32 icon in a DS ROM's banner (the DS menu's picture of the game), RGBA8 with palette index 0 transparent:
	// from a .nds, or from a .zip's first .nds (else its first file) through the host's zip reader. Empty when there
	// is none: no banner, a banner offset inside the header or past the end (or, in a zip, more than 32 MB in), a banner
	// version other than 1, 2, 3 or 0x103, another kind of file. Remembered per path (the last 512).
	Bitmap romIcon(const std::string& path);
	// the icon from a banner's first 0x240 bytes: version, CRCs, the 4x4 tiles of 8x8 4bpp pixels at 0x20 (low nibble
	// first), the 16 BGR555 colours at 0x220
	Bitmap ndsBannerIcon(const uint8_t* banner);
	// the host's zip archives (ES: es-core's Utils::Zip::ZipFile): pick chooses a member from the list (its index, -1
	// for none), sink takes that member's bytes in order until it returns false; false when the archive can't be read
	struct ZipMember { std::string name; uint64_t size = 0; };
	typedef bool (*ZipReader)(const std::string& zip, const std::function<int(const std::vector<ZipMember>&)>& pick,
		const std::function<bool(const uint8_t*, size_t)>& sink);
	void setZipReader(ZipReader reader);
}
