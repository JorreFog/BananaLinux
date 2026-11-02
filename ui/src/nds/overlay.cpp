// banana-ui: the DS companion's overlay (overlay.h).
#include "overlay.h"
#include "../lang.h"
#include "../settings_draw.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <poll.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

namespace banana::nds
{
	using rnds::Bitmap;
	using rnds::Color;

	static double monoMs()
	{
		timespec t;
		clock_gettime(CLOCK_MONOTONIC, &t);
		return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
	}

	// ---- the file ----

	bool Overlay::create(int w, int h, const std::string& p)
	{
		close();
		shm = bovl_create(p.c_str(), (uint32_t)w, (uint32_t)h, 2);
		if (!shm) return false;
		path = p;
		W = w;
		H = h;
		be = std::make_unique<SoftBackend>(w, h);
		bovl_painter_start(shm);
		dirty = { 0, 0, 0, 0 };
		dirtyPending = seqPending = false;
		sentSeq = 0;
		touchTail = 0;
		iconBadge = -1;
		iconW = iconH = 0;
		framesSent = bytesSent = 0;
		return true;
	}

	void Overlay::close()
	{
		if (!shm) return;
		bovl_painter_stop(shm);
		bovl_unmap(shm);
		unlink(path.c_str());
		shm = nullptr;
		be.reset();
	}

	bool Overlay::presenter() const
	{
		return shm && __atomic_load_n(&shm->presenter_state, __ATOMIC_ACQUIRE) == BOVL_PRESENTER_READY;
	}

	void Overlay::requestOpen(bool open)
	{
		if (shm) bovl_request_open(shm, open ? 1 : 0);
	}

	// ---- the icon: the mockup's (icon-rgds-badge.png) at the panel's scale ----

	static Bitmap box(int w, int h, float radius, Color bg, float border = 0, Color edge = Color(), int* ox = nullptr, int* oy = nullptr,
		const std::vector<rnds::Shadow>& shadows = {})
	{
		rnds::BoxStyle st;
		st.radius = radius;
		st.bg = bg;
		st.border = border;
		st.borderColor = edge;
		st.shadows = shadows;
		rnds::PaintedBox pb = rnds::paintBox(st, w, h);
		if (ox) *ox = pb.originX;
		if (oy) *oy = pb.originY;
		return pb.bmp;
	}

	// coverage (A8) to straight-alpha RGBA in one colour
	static Bitmap tint(const Bitmap& a8, uint32_t rgb)
	{
		Bitmap o;
		o.w = a8.w;
		o.h = a8.h;
		o.px.resize((size_t)o.w * o.h * 4);
		for (size_t i = 0; i < (size_t)o.w * o.h; i++)
		{
			o.px[i * 4] = (uint8_t)(rgb >> 16);
			o.px[i * 4 + 1] = (uint8_t)(rgb >> 8);
			o.px[i * 4 + 2] = (uint8_t)rgb;
			o.px[i * 4 + 3] = a8.px[i];
		}
		return o;
	}

	static std::string assetsDir = "/usr/share/banana/rnds";
	static std::shared_ptr<rnds::Font> fonts[2];
	void setAssets(const std::string& dir)
	{
		if (dir == assetsDir) return;
		assetsDir = dir;
		fonts[0] = fonts[1] = nullptr;
	}
	static std::shared_ptr<rnds::Font> font(bool medium)
	{
		if (!fonts[medium]) fonts[medium] = rnds::Font::load(assetsDir + (medium ? "/fonts/PixelifySans-Medium.ttf" : "/fonts/PixelifySans-Regular.ttf"));
		return fonts[medium];
	}

	// a line of text as an A8 bitmap at size (CSS px) drawn at scale k; pad: where its pen origin is in it
	static Bitmap textA8(const std::string& t, bool medium, float size, float k, int* padX = nullptr, int* padY = nullptr, float* width = nullptr)
	{
		rnds::TextStyle st;
		st.font = font(medium);
		st.size = size;
		if (!st.font || t.empty()) return Bitmap();
		rnds::TextLine l = rnds::layoutLine(rnds::utf8to32(t), st);
		rnds::TextBitmap tb = rnds::renderLine(l, st, k, st.font->ascent(size));
		if (padX) *padX = tb.padX;
		if (padY) *padY = tb.padY;
		if (width) *width = l.width * k;
		return tb.bmp;
	}

	void Overlay::setIcon(bool on, int badge)
	{
		if (!shm) return;
		badge = std::max(0, badge);
		if (on == iconOn && badge == iconBadge && iconW) return;
		iconOn = on;
		iconBadge = badge;
		const float k = scale();
		const int S = (int)std::lround(44 * k), O = (int)std::lround(9 * k), D = (int)std::lround(3 * k);
		// the plate in the corner itself, the badge over its inner corner (towards the middle of the screen), so the badge
		// never hangs off the panel: up and left of the plate in the bottom-right corner, down and left in the top-right
		const bool right = ICON_CORNER == BOVL_CORNER_BR || ICON_CORNER == BOVL_CORNER_TR;
		const bool bottom = ICON_CORNER == BOVL_CORNER_BR || ICON_CORNER == BOVL_CORNER_BL;
		const int plateX = right ? O : 0, plateY = bottom ? O : 0;
		Bitmap pic;
		pic.w = O + S;
		pic.h = bottom ? O + S + D : S + std::max(O, D);
		pic.px.assign((size_t)pic.w * pic.h * 4, 0);
		const Color cream(236 / 255.0f, 228 / 255.0f, 210 / 255.0f, 1), ink = Color::hex(0x1c1b21);
		int ox, oy;
		// the plate: the theme's accent, a cream lip, a drop under it; see-through so the game shows under the corner
		Color plate = Color::hex(rnds::accent.base, 0.75f);
		Color edge = cream;
		edge.a = 0.86f;
		Bitmap b = box(S, S, 10 * k, plate, std::max(1.0f, std::round(3 * k)), edge, &ox, &oy,
			{ rnds::Shadow{ 0, (float)D, 0, Color(12 / 255.0f, 11 / 255.0f, 15 / 255.0f, 0.6f), false } });
		rnds::blit(pic, b, plateX - ox, plateY - oy);
		// the glyph: two pages, the front one with three lines (the companion's pages)
		Color back = ink;
		back.a = 0.9f;
		rnds::blit(pic, box((int)std::lround(18 * k), (int)std::lround(22 * k), 0, back), plateX + (int)std::lround(12 * k), plateY + (int)std::lround(10 * k));
		Color page = cream;
		page.a = 0.94f;
		rnds::blit(pic, box((int)std::lround(18 * k), (int)std::lround(22 * k), 0, page), plateX + (int)std::lround(16 * k), plateY + (int)std::lround(14 * k));
		for (int i = 0; i < 3; i++)
			rnds::blit(pic, box((int)std::lround(12 * k), std::max(1, (int)std::lround(2 * k)), 0, ink),
				plateX + (int)std::lround(19 * k), plateY + (int)std::lround((19 + 5 * i) * k));
		// the badge: a red dot with the count over the plate's inner corner
		if (badge > 0)
		{
			const int B = (int)std::lround(22 * k);
			const int bx = right ? 0 : pic.w - B, by = bottom ? 0 : S + O - B;
			rnds::blit(pic, box(B, B, B / 2.0f, Color::hex(0xe8574d), std::max(1.0f, std::round(2 * k)), cream), bx, by);
			int px = 0, py = 0;
			float tw = 0;
			const std::string n = badge > 9 ? "9+" : std::to_string(badge);
			Bitmap t = textA8(n, true, badge > 9 ? 12 : 15, k, &px, &py, &tw);
			if (!t.empty())
			{
				rnds::TextStyle st;
				st.font = font(true);
				const float asc = st.font ? st.font->ascent(badge > 9 ? 12 : 15) * k : 0, desc = st.font ? st.font->descent(badge > 9 ? 12 : 15) * k : 0;
				rnds::blit(pic, tint(t, 0xffffff), bx + (int)std::lround((B - tw) / 2) - px, by + (int)std::lround((B - (asc + desc)) / 2) - py);
			}
		}
		// premultiplied 0xAARRGGBB for the file
		std::vector<uint32_t> px((size_t)pic.w * pic.h);
		for (size_t i = 0; i < px.size(); i++)
		{
			const uint32_t a = pic.px[i * 4 + 3];
			const uint32_t r = (pic.px[i * 4] * a + 127) / 255, g = (pic.px[i * 4 + 1] * a + 127) / 255, bl = (pic.px[i * 4 + 2] * a + 127) / 255;
			px[i] = a << 24 | r << 16 | g << 8 | bl;
		}
		iconW = pic.w;
		iconH = pic.h;
		// in its corner, 2 px in; taps reach iconSlop() around the picture (BananaDS: while closed)
		bovl_set_icon(shm, px.data(), (uint32_t)pic.w, (uint32_t)pic.h, on ? 1 : 0, (uint32_t)badge, ICON_CORNER,
			(uint32_t)std::lround(2 * k), (uint32_t)iconSlop());
	}

	int Overlay::iconSlop() const
	{
		// a fingertip is ~8 mm across: a tap whose finger touches the plate counts, so the tap area reaches half of that
		// (4 mm) past the plate's edge. The picture already holds the badge's room (9 px of the look) on the plate's inner
		// sides; the slop is what is still missing to 4 mm (BananaDS takes it only while closed: open, around the icon is
		// the page)
		// (the panels' width, from the board's device trees: RG DS 81 mm at 640 px, RG DS Plus 87 mm at 1024 px)
		const float k = scale();
		const int fromPlate = (int)std::lround(ICON_SLOP_MM * W / (W >= 1024 ? 87.0 : 81.0));
		return std::max((int)std::lround(6 * k), fromPlate - (int)std::lround(9 * k));
	}

	Rect Overlay::iconRect() const
	{
		if (!shm) return Rect();
		const bovl_rect r = shm->icon_rect;
		if (r.w > 0 && r.h > 0) return Rect{ r.x, r.y, r.w, r.h };
		const int m = (int)std::lround(2 * scale());
		const bool right = ICON_CORNER == BOVL_CORNER_BR || ICON_CORNER == BOVL_CORNER_TR;
		const bool bottom = ICON_CORNER == BOVL_CORNER_BR || ICON_CORNER == BOVL_CORNER_BL;
		return Rect{ right ? W - m - iconW : m, bottom ? H - m - iconH : m, iconW, iconH };
	}

	// ---- frames ----

	void Overlay::begin()
	{
		be->beginScreen(1);
	}

	bool Overlay::end()
	{
		be->endScreen();
		const Damage d = be->takeDamage(1);
		if (!d.empty())
		{
			dirty = bovl_union(dirty, bovl_rect{ d.x0, d.y0, d.x1 - d.x0, d.y1 - d.y0 });
			dirtyPending = true;
		}
		return send();
	}

	bool Overlay::send()
	{
		if (!shm) return false;
		const uint32_t seq = openSeq();
		seqPending = seq != sentSeq;
		if (!dirtyPending && !seqPending) return true;
		// (a new opening with nothing changed still gets its own frame: BananaDS shows only a picture made for it)
		if (!bovl_paint_xrgb(shm, be->pixels(1), (uint32_t)W, dirty, seq)) return false;
		framesSent++;
		bytesSent += (uint64_t)std::max(0, dirty.w) * std::max(0, dirty.h) * 2;
		dirty = { 0, 0, 0, 0 };
		dirtyPending = seqPending = false;
		sentSeq = seq;
		return true;
	}

	bool Overlay::pollTouch(Touch& t)
	{
		bovl_touch b;
		if (!shm || !bovl_touch_next(shm, &touchTail, &b, nullptr)) return false;
		t.type = (Touch::Type)b.type;
		t.x = b.x;
		t.y = b.y;
		t.ms = b.ms;
		return true;
	}

	// ---- the test page ----

	namespace
	{
		class TestPage : public OverlayPage
		{
		public:
			explicit TestPage(Overlay& o) : ov(o), k(o.scale()) {}
			void opened() override { openedAt = monoMs(); }
			bool tick(double now) override
			{
				bool want = false;
				const int sec = (int)((now - openedAt) / 1000);
				if (sec != lastSec)	// (the counters move on once a second, as a page's clock would)
				{
					lastSec = sec;
					want = true;
					sent = ov.framesSent;
					sentKB = ov.bytesSent / 1024.0;
					copied = ov.raw() ? ov.raw()->st_copies : 0;
					touchesShown = touches;
				}
				// (a 20 fps animation the size of a Dex sprite drawn twice as big: Gen V's idle animations run at 20; the
				// counters are read when it draws, never a reason to draw: each picture sent would count as a change)
				const int f = (int)(now / 50) % 8;
				if (f != animF) { animF = f; want = true; }
				return want;
			}
			void touch(const Touch& t) override
			{
				touches++;
				lastX = t.x;
				lastY = t.y;
				down = t.type != Touch::UP;
				if (t.type == Touch::UP && backBox.in(t.x, t.y)) closeAsked = true;	// (the button's part BananaDS doesn't take)
			}
			bool wantsClose() override
			{
				const bool c = closeAsked;
				closeAsked = false;
				return c;
			}
			int badge() const override { return 2; }	// (the mockup's: shows the badge's look)
			void draw(rnds::Backend& be, int w, int h, const Rect& back) override
			{
				const SPal p = pal(false);
				if (!bg)
				{
					rnds::BoxStyle st;
					st.bg = p.screenBg;
					st.dither = true;
					st.ditherScale = k;
					st.ditherA = p.ditherA;
					st.ditherB = p.ditherB;
					bg = be.upload(rnds::paintBox(st, w, h).bmp, false);
				}
				be.draw(bg, 0, 0, (float)w, (float)h, Color(1, 1, 1, 1));
				// the card
				const int cx = X(20), cy = X(18), cw = w - X(40), ch = X(300);
				if (!card)
				{
					int ox, oy;
					Bitmap c = box(cw, ch, 12 * k, p.panelBody, std::max(1.0f, std::round(3 * k)), Color::hex(0xece4d2), &ox, &oy,
						{ rnds::Shadow{ 0, 4 * k, 0, Color(0, 0, 0, 0.45f), false } });
					card = be.upload(c, false);
					cardOx = ox;
					cardOy = oy;
				}
				be.draw(card, (float)(cx - cardOx), (float)(cy - cardOy), (float)card->w, (float)card->h, Color(1, 1, 1, 1));
				text(be, lang::tr("Companion overlay test"), true, 22, cx + X(18), cy + X(14), p.ink);
				char b[160];
				snprintf(b, sizeof b, "%s %llu (%.0f KB)", lang::tr("Pictures sent:").c_str(), (unsigned long long)sent, sentKB);
				text(be, b, false, 16, cx + X(18), cy + X(52), p.ink);
				snprintf(b, sizeof b, "%s %llu, %s %llu", lang::tr("BananaDS copied:").c_str(), (unsigned long long)copied,
					lang::tr("touches:").c_str(), (unsigned long long)touchesShown);
				text(be, b, false, 16, cx + X(18), cy + X(76), p.ink);
				snprintf(b, sizeof b, "%s %d, %d", lang::tr("Last touch:").c_str(), lastX, lastY);
				text(be, b, false, 16, cx + X(18), cy + X(100), p.muted);
				snprintf(b, sizeof b, "%s %d:%02d", lang::tr("Open for:").c_str(), lastSec / 60, lastSec % 60);
				text(be, b, false, 16, cx + X(18), cy + X(124), p.muted);
				text(be, lang::tr("The game keeps running and keeps its buttons."), false, 14, cx + X(18), cy + X(160), p.muted);
				text(be, lang::tr("Touch anywhere: a dot follows your finger."), false, 14, cx + X(18), cy + X(182), p.muted);
				// a stand-in for an animated Dex sprite: 128 px, a new frame twenty times a second
				const int sx = cx + cw - X(18) - X(128), sy = cy + X(56), sw = X(128);
				be.fill((float)sx, (float)sy, (float)sw, (float)sw, Color::hex(0x24222c));
				const float bob = (animF % 4 < 2 ? 0 : 4) * k;
				be.fill((float)(sx + X(28)), (float)(sy + X(30)) + bob, (float)X(56), (float)X(50), Color::hex(rnds::accent.base));
				be.fill((float)(sx + X(38) + (animF % 2) * X(4)), (float)(sy + X(44)) + bob, (float)X(8), (float)X(8), Color::hex(0x1c1b21));
				be.fill((float)(sx + X(66) + (animF % 2) * X(4)), (float)(sy + X(44)) + bob, (float)X(8), (float)X(8), Color::hex(0x1c1b21));
				be.fill((float)(sx + X(20) + animF * X(9)), (float)(sy + X(92)), (float)X(10), (float)X(6), Color::hex(0xe8574d));
				// the finger
				if (down)
				{
					if (!dot)
					{
						const int d = X(28);
						dot = be.upload(box(d, d, d / 2.0f, Color::hex(rnds::accent.base, 0.85f), std::max(1.0f, std::round(2 * k)), Color::hex(0xece4d2)), true);
					}
					be.draw(dot, (float)(lastX - dot->w / 2), (float)(lastY - dot->h / 2), (float)dot->w, (float)dot->h, Color(1, 1, 1, 1));
				}
				// Back to game, ending where the icon is: BananaDS takes the taps on the icon's place itself, the rest
				// of the button comes here (wantsClose)
				const int bh = X(40), bw = X(190);
				const bool top = back.y < h / 2;
				backBox = Rect{ back.x + back.w - bw, top ? back.y + X(3) : back.y + back.h - bh - X(3), bw, bh };
				if (!backTex)
				{
					backTex = be.upload(box(bw, bh, 10 * k, Color::hex(rnds::accent.base), std::max(1.0f, std::round(3 * k)), Color::hex(0xece4d2)), false);
				}
				be.draw(backTex, (float)backBox.x, (float)backBox.y, (float)backTex->w, (float)backTex->h, Color(1, 1, 1, 1));
				const Color on = Color::hex(rnds::inkOn(rnds::accent.base));
				text(be, lang::tr("Back to game"), true, 16, backBox.x + X(16), backBox.y + X(11), on);
				(void)h;
			}

		private:
			Overlay& ov;
			float k;
			double openedAt = monoMs();
			int lastSec = -1, animF = -1, lastX = 0, lastY = 0;
			uint64_t touches = 0, sent = 0, copied = 0, touchesShown = 0;
			double sentKB = 0;
			bool down = false, closeAsked = false;
			Rect backBox;
			rnds::TexPtr bg, card, dot, backTex;
			int cardOx = 0, cardOy = 0;
			struct T { rnds::TexPtr tex; int px = 0, py = 0; };
			std::map<std::string, T> cache;	// (the same text keeps its texture: SoftBackend redraws only what changed)
			int X(float css) const { return (int)std::lround(css * k); }
			void text(rnds::Backend& be, const std::string& t, bool medium, float size, int x, int y, const Color& c)
			{
				const std::string key = t + (medium ? "|m" : "|r") + std::to_string(size);
				auto it = cache.find(key);
				if (it == cache.end())
				{
					if (cache.size() > 256) cache.clear();
					T e;
					Bitmap a = textA8(t, medium, size, k, &e.px, &e.py);
					if (!a.empty()) e.tex = be.upload(a, true);
					it = cache.emplace(key, e).first;
				}
				if (it->second.tex)
					be.draw(it->second.tex, (float)(x - it->second.px), (float)(y - it->second.py), (float)it->second.tex->w, (float)it->second.tex->h, c);
			}
		};
	}

	std::unique_ptr<OverlayPage> makeTestPage(Overlay& ov)
	{
		return std::make_unique<TestPage>(ov);
	}

	// ---- the loop ----

	void runWhileGame(Overlay& ov, OverlayPage& page, pid_t pid, int& status, const std::function<void(double)>& idle)
	{
		const int pidFd = (int)syscall(SYS_pidfd_open, pid, 0);
		uint32_t seenSeq = ov.openSeq();
		bool wasShown = false, finger = false, want = true;
		double lastDraw = 0;
		const char* CMD = "/run/banana/companion-cmd";	// (tests over SSH: "open", "close", "tap x y")
		for (;;)
		{
			const pid_t w = waitpid(pid, &status, WNOHANG);
			if (w == pid || (w < 0 && errno != EINTR)) break;
			const double now = monoMs();
			if (idle) idle(now);
			ov.setIcon(page.iconOn(), page.badge());
			if (access(CMD, F_OK) == 0)
			{
				std::ifstream f(CMD);
				std::string c;
				f >> c;
				unlink(CMD);
				if (c == "open" || c == "close") ov.requestOpen(c == "open");
				else if (c == "tap")
				{
					Touch t;
					f >> t.x >> t.y;
					t.type = Touch::DOWN;
					page.touch(t);
					t.type = Touch::UP;
					page.touch(t);
					want = true;
				}
			}
			const bool shown = ov.shown();
			if (shown && (!wasShown || ov.openSeq() != seenSeq))
			{
				seenSeq = ov.openSeq();
				page.opened();
				want = true;
			}
			wasShown = shown;
			Touch t;
			while (ov.pollTouch(t))
			{
				page.touch(t);
				finger = t.type != Touch::UP;
				want = true;
			}
			if (page.wantsClose()) ov.requestOpen(false);
			if (page.tick(now)) want = true;
			int timeout = shown ? 16 : 50;
			if (shown && (want || ov.pending()))
			{
				const double gap = finger ? 16 : 33;	// (30 pictures a second at most; 60 while a finger drags)
				if (now - lastDraw >= gap)
				{
					if (want)
					{
						ov.begin();
						page.draw(ov.backend(), ov.width(), ov.height(), ov.iconRect());
						ov.end();
						want = false;
					}
					else ov.send();
					lastDraw = now;
				}
				else timeout = std::max(1, (int)(gap - (now - lastDraw)));
			}
			pollfd p{ pidFd, POLLIN, 0 };
			if (pidFd >= 0) poll(&p, 1, timeout);
			else usleep(timeout * 1000);
		}
		if (pidFd >= 0) ::close(pidFd);
	}
}
