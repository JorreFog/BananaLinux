#include "arrival.h"
#include "../rnds/RndsUI.h"
#include <algorithm>
#include <cmath>

namespace banana
{
	// the two panels as one tall picture, in CSS px of a 640x480 panel: the top one at 0..480, the hinge, the bottom one
	static constexpr float GAP = 48, BOT_Y = 480 + GAP;
	// the zoom: the middle of the cartridge as it sits in the console (the boot animation's last frame) to the middle of
	// the bottom panel, 4x; the cartridge's top edge is then at CART_TOP on the bottom panel
	static constexpr float ANCHOR_X = 320, ANCHOR_Y = 300, ZOOM = 4, CART_TOP = 240 + (278 - ANCHOR_Y) * ZOOM;
	static constexpr double LAUNCH = Arrival::RUMBLE_END + 100;	// the first banana out
	static constexpr double CAM_AT = 1450;						// the camera starts to follow (the lead nears the top panel)
	static constexpr double RAMP = 700;							// ... reaching full speed over this
	static constexpr float LOCK_Y = 190;						// where the lead stays: the top panel, a little above the middle
	static constexpr uint32_t SKY = 0xff141219u;

	// one banana, the bunch's own colours (assets/banana: outline, highlight, yellow, mid, shade, brown, stem)
	static const char* const FRUIT[16] = {
		"................",
		"............OO..",
		"...........OSSO.",
		"...........OBBO.",
		"..........OYDO..",
		"..........OYMDO.",
		".........OLYMDO.",
		"........OLYYMDO.",
		".......OLYYYMDO.",
		"......OLYYYMDO..",
		"....OOLYYYMMDO..",
		"..OOLLYYYMMDO...",
		"OOBLYYYMMMDO....",
		"OBBMMMMMDDO.....",
		".OODDDDDOO......",
		"...OOOOO........",
	};
	static uint32_t fruitColour(char c)
	{
		switch (c)
		{
		case 'O': return 0xff000000u;
		case 'L': return 0xfffff4aau;
		case 'Y': return 0xffffd640u;
		case 'M': return 0xfff6b21cu;
		case 'D': return 0xffd68412u;
		case 'B': return 0xff965210u;
		case 'S': return 0xff5c3816u;
		default: return 0;
		}
	}

	static inline uint32_t mul255(uint32_t p, uint32_t k)
	{
		uint32_t rb = (p & 0x00ff00ffu) * k + 0x00800080u;
		rb = ((rb + ((rb >> 8) & 0x00ff00ffu)) >> 8) & 0x00ff00ffu;
		uint32_t ag = ((p >> 8) & 0x00ff00ffu) * k + 0x00800080u;
		ag = (ag + ((ag >> 8) & 0x00ff00ffu)) & 0xff00ff00u;
		return rb | ag;
	}
	// a premultiplied colour over an opaque one
	static inline uint32_t over(uint32_t s, uint32_t d)
	{
		uint32_t a = s >> 24;
		if (a == 255) return s;
		if (a == 0) return d;
		return s + mul255(d, 255 - a);
	}
	// an opaque colour a/255 of the way to white
	static inline uint32_t toWhite(uint32_t c, uint32_t a)
	{
		return mul255(c | 0xff000000u, 255 - a) + a * 0x01010101u;
	}
	static inline float clamp01(double v) { return (float)std::max(0.0, std::min(1.0, v)); }
	static inline float smooth(float p) { return p * p * (3 - 2 * p); }
	// how far the camera has gone at t (the stars' and the console's travel): standing, then speeding up over RAMP, then
	// at full speed (1 per ms)
	static double travel(double t)
	{
		if (t < CAM_AT) return 0;
		double q = t - CAM_AT;
		return q < RAMP ? q * q / (2 * RAMP) : RAMP / 2 + (q - RAMP);
	}
	static double speed(double t) { return t < CAM_AT ? 0 : std::min(1.0, (t - CAM_AT) / RAMP); }

	int Arrival::X(float v) const { return (int)std::lround(v * s); }

	bool Arrival::load(const std::string& dir, int w, int h)
	{
		W = w; H = h; s = h / 480.0f;
		u = std::max(1, (int)std::lround(2 * s));
		rnds::Bitmap b = rnds::decodeFile(dir + "/word.png");
		if (b.empty()) return false;
		wordW = b.w; wordH = b.h;
		word.resize((size_t)b.w * b.h);
		for (size_t i = 0; i < word.size(); i++) word[i] = b.px[i * 4] > 127;
		// the bananas: the lead first, then the ones behind it in a loose V, smaller (further back) the lower they fly;
		// each its own launch, speed, sway and spin (a fixed list: the same picture every boot and in the tests)
		struct F { float x0, vx, vy, launch, slotX, slotY; int k, spin; };
		static const F list[] = {
			{ 320, 0, 0, 0, 320, LOCK_Y, 6, 0 },
			{ 250, -260, 1250, 40, 196, 318, 4, 0 },
			{ 392, 300, 1320, 90, 452, 336, 4, 3 },
			{ 300, -420, 1180, 140, 100, 430, 3, 4 },
			{ 352, 380, 1240, 180, 548, 452, 3, -3 },
			{ 280, -120, 1380, 230, 250, 586, 3, 5 },
			{ 368, 160, 1300, 270, 420, 640, 3, -4 },
			{ 240, -520, 1100, 320, 92, 700, 2, 6 },
			{ 400, 520, 1150, 350, 560, 742, 2, -5 },
			{ 320, -40, 1420, 390, 330, 790, 2, 4 },
			{ 270, -300, 1200, 430, 190, 880, 2, -6 },
			{ 372, 330, 1260, 470, 472, 912, 2, 5 },
		};
		fruit.clear();
		int i = 0;
		for (const F& f : list)
		{
			Fruit r;
			r.x0 = f.x0; r.vx = f.vx; r.vy = f.vy; r.launch = (float)LAUNCH + f.launch;
			r.slotX = f.slotX; r.slotY = f.slotY; r.k = f.k; r.spin = f.spin;
			r.sway = i ? 10.0f + 4 * (i % 3) : 0; r.swayRate = 0.0023f + 0.0004f * (i % 4); r.phase = 1.7f * i;
			fruit.push_back(r);
			i++;
		}
		// the stars, over both panels and the hinge: three layers, the nearer ones brighter and faster
		stars.clear();
		uint32_t seed = 0x8a5c3d1u;
		auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0f; };
		for (int n = 0; n < 110; n++) stars.push_back({ rnd() * 640, rnd() * (BOT_Y + 480), n % 7 == 0 ? 2 : n % 3 == 0 ? 1 : 0 });
		return true;
	}

	void Arrival::setStart(const uint32_t* top, const uint32_t* bot)
	{
		const size_t n = (size_t)W * H;
		startTop.assign(top, top + n);
		startBot.assign(bot, bot + n);
		// (made now, while the boot animation's last frame is up: a step made on its frame cost a refresh on the Plus)
		for (int i = 0; i < 3; i++)
		{
			uint32_t k = 191 - 64 * i;
			dim[i].resize(n);
			for (size_t j = 0; j < n; j++) dim[i][j] = mul255(startTop[j], k) + mul255(SKY, 255 - k);
		}
	}

	uint32_t Arrival::C(uint32_t c) const { return wl ? toWhite(c, wl) : c; }

	void Arrival::setCaption(const uint32_t* top)
	{
		caption.assign(top, top + (size_t)W * H);
		cardMade = false;
	}

	void Arrival::rect(uint32_t* d, int x, int y, int w, int h, uint32_t c) const
	{
		int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(W, x + w), y1 = std::min(H, y + h);
		if (x0 >= x1 || y0 >= y1) return;
		bool solid = (c >> 24) == 255;
		for (int yy = y0; yy < y1; yy++)
		{
			uint32_t* row = d + (size_t)yy * W;
			if (solid) std::fill(row + x0, row + x1, c);
			else for (int xx = x0; xx < x1; xx++) row[xx] = over(c, row[xx]);
		}
	}

	// the card: the caption (as the host drew it) with the BANANAOS word under it, as the boot animation draws it (two
	// banana tones, OS in white, a shadow), at whole device px per word pixel
	void Arrival::makeCard()
	{
		cardMade = true;
		card.assign((size_t)W * H, 0xff000000u);
		if (caption.size() == card.size()) card = caption;
		const int kw = std::max(1, (int)std::lround(6 * s)), sh = std::max(1, kw / 3);
		const int x0 = (W - wordW * kw) / 2, y0 = X(222);
		// the letters' first columns (the empty columns between them): OS is the last two
		std::vector<int> starts;
		bool prev = false;
		for (int x = 0; x < wordW; x++)
		{
			bool any = false;
			for (int y = 0; y < wordH; y++) any |= word[(size_t)y * wordW + x] != 0;
			if (any && !prev) starts.push_back(x);
			prev = any;
		}
		int osFrom = starts.size() >= 2 ? starts[starts.size() - 2] : wordW;
		for (int pass = 0; pass < 2; pass++)
			for (int my = 0; my < wordH; my++)
				for (int mx = 0; mx < wordW; mx++)
				{
					if (!word[(size_t)my * wordW + mx]) continue;
					uint32_t c = pass == 0 ? 0xff000000u : mx >= osFrom ? 0xffffffffu :
						my < (wordH * 11 + 19) / 20 ? rnds::tintAccent(0xffffde52u) : rnds::tintAccent(0xffeea018u);
					int o = pass == 0 ? sh : 0;
					rect(card.data(), x0 + mx * kw + o, y0 + my * kw + o, kw, kw, c);
				}
	}

	// the boot animation's bottom frame, nearest-neighbour: the start picture's point (ax, ay) at (cx, cy) on the panel,
	// z times as big, dy device px lower (the camera leaving it behind); the sky above it
	void Arrival::zoomed(uint32_t* d, const std::vector<uint32_t>& src, float ax, float ay, float cx, float cy, float z, int dy) const
	{
		static thread_local std::vector<int> xs;
		xs.resize(W);
		for (int x = 0; x < W; x++) xs[x] = std::max(0, std::min(W - 1, (int)std::floor(ax + (x + 0.5f - cx) / z)));
		int lastSy = -2;
		for (int y = 0; y < H; y++)
		{
			uint32_t* row = d + (size_t)y * W;
			int sy = (int)std::floor(ay + (y - dy + 0.5f - cy) / z);
			if (y < dy || sy < 0) { std::fill(row, row + W, C(SKY)); lastSy = -2; continue; }
			sy = std::min(H - 1, sy);
			// (zoomed in, rows repeat: the one above copied instead of gathered again)
			if (sy == lastSy) { std::copy(row - W, row, row); continue; }
			lastSy = sy;
			const uint32_t* sr = src.data() + (size_t)sy * W;
			for (int x = 0; x < W; x++) row[x] = sr[xs[x]];
		}
	}

	// the night sky on one panel (0 top, 1 bottom): the stars as dots, streaks once the camera flies
	void Arrival::sky(uint32_t* d, int screen, double t) const
	{
		static const uint32_t ink[3] = { 0xff3a3648u, 0xff8a8578u, 0xfff1ede2u };
		static const float pace[3] = { 0.22f, 0.45f, 0.85f };	// CSS px per ms at full speed
		const float span = BOT_Y + 480, tr = (float)travel(t), sp = (float)speed(t);
		const float y0 = screen ? BOT_Y : 0;
		for (const Star& st : stars)
		{
			float y = std::fmod(st.y + tr * pace[st.layer], span);
			float len = std::max(1.0f, std::round(sp * pace[st.layer] * 16.7f / 2.0f * 1.5f));	// art px: about a frame's travel
			float yy = y - y0;
			if (yy + len * 2 < 0 || yy - len * 2 > 480) continue;
			int px = X(std::floor(st.x / 2) * 2), py = X(std::floor(yy / 2) * 2);
			int w = st.layer == 2 && sp == 0 ? 2 * u : u;
			rect(d, px, py - (int)(len - 1) * u, w, (int)len * u, C(ink[st.layer]));
		}
	}

	// a banana centred on (x, y) (CSS px of the two panels), k art px per sprite pixel, turned by quarter turns, white
	// steps of 4 (0 none, 4 all white); on the panels it is over
	void Arrival::banana(uint32_t* top, uint32_t* bot, float x, float y, int k, int turn, int white) const
	{
		const int px = k * u, size = 16 * px;
		for (int screen = 0; screen < 2; screen++)
		{
			uint32_t* d = screen ? bot : top;
			int ox = X(x) - size / 2, oy = X(y - (screen ? BOT_Y : 0)) - size / 2;
			if (oy + size <= 0 || oy >= H || ox + size <= 0 || ox >= W) continue;
			for (int sy = 0; sy < 16; sy++)
				for (int sx = 0; sx < 16; sx++)
				{
					// the sprite pixel shown at (sx, sy) for this turn
					int fx = sx, fy = sy;
					switch (turn & 3)
					{
					case 1: fx = sy; fy = 15 - sx; break;
					case 2: fx = 15 - sx; fy = 15 - sy; break;
					case 3: fx = 15 - sy; fy = sx; break;
					default: break;
					}
					uint32_t c = fruitColour(FRUIT[fy][fx]);
					if (!c) continue;
					if (c != 0xff000000u) c = rnds::tintAccent(c);
					// (the outline later than the fruit: a pale grey outline round a white banana read as a box)
					int w = c == 0xff000000u ? white * 64 - 128 : white * 64;
					if (w > 0) c = toWhite(c, (uint32_t)std::min(255, w));
					rect(d, ox + sx * px, oy + sy * px, px, px, C(c));
				}
		}
	}

	// the light round the lead as it lights up: a soft disc and rings running out, in blocks of the look's pixels
	void Arrival::halo(uint32_t* top, uint32_t* bot, float cx, float cy, double t) const
	{
		float p = clamp01((t - FOLLOW_END) / (WHITE - FOLLOW_END));
		if (p <= 0) return;
		const int B = 2 * u;
		const float step = 8;	// the rings move in steps of 8 CSS px
		float r0 = std::floor(p * 260 / step) * step;		// the disc
		float r1 = std::floor(p * 1100 / step) * step;		// the first ring
		const float ringW = 10;
		for (int screen = 0; screen < 2; screen++)
		{
			uint32_t* d = screen ? bot : top;
			float off = screen ? BOT_Y : 0;
			for (int by = 0; by < H; by += B)
				for (int bx = 0; bx < W; bx += B)
				{
					float x = (bx + B * 0.5f) / s, y = (by + B * 0.5f) / s + off;
					float dist = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
					uint32_t a = 0;
					if (dist < r0) a = dist < r0 * 0.6f ? 150 : 90;
					for (int j = 0; j < 3; j++)
					{
						float r = r1 - j * 70;
						if (r > 0 && std::fabs(dist - r) < ringW / 2) a = std::max<uint32_t>(a, 200 - 60 * j);
					}
					// (over the sky it is drawn on: a solid colour, a fill instead of a blend)
					if (a) rect(d, bx, by, B, B, C(over(mul255(rnds::tintAccent(0xfffff4aau), a), SKY)));
				}
		}
	}

	// what the frame at t is, for the phases that stand still (-1: moving)
	long Arrival::key(double t) const
	{
		if (t >= WHITE && t < BLACK_AT) return 1;
		if (t >= BLACK_AT && t < CARD_AT) return 2;
		if (t >= CARD_AT && t < CARD_AT + CARD_IN) return 10 + (long)((t - CARD_AT) / (CARD_IN / 4));
		if (t >= CARD_AT + CARD_IN && t < CARD_AT + CARD_IN + CARD_HOLD) return 3;
		if (t >= CARD_AT + CARD_IN + CARD_HOLD && t < CARD_AT + CARD_IN + CARD_HOLD + CARD_OUT)
			return 20 + (long)((t - CARD_AT - CARD_IN - CARD_HOLD) / (CARD_OUT / 4));
		if (t >= CARD_AT + CARD_IN + CARD_HOLD + CARD_OUT) return 4;
		return -1;
	}

	bool Arrival::still(double t) const { long k = key(t); return k >= 0 && k == lastKey; }

	void Arrival::draw(uint32_t* top, uint32_t* bot, double t)
	{
		lastKey = key(t);
		const size_t n = (size_t)W * H;
		// ---- the white, the black and the card ----
		if (t >= WHITE)
		{
			if (!flashCued) { flashCued = true; if (onCue) onCue("flash"); }
			if (t < BLACK_AT) { std::fill(top, top + n, 0xffffffffu); std::fill(bot, bot + n, 0xffffffffu); return; }
			std::fill(bot, bot + n, 0xff000000u);
			double c = t - CARD_AT;
			int level = 0;	// the card's brightness, in four steps up and four down
			if (c >= 0 && c < CARD_IN) level = 1 + (int)(c / (CARD_IN / 4));
			else if (c >= CARD_IN && c < CARD_IN + CARD_HOLD) level = 4;
			else if (c >= CARD_IN + CARD_HOLD && c < CARD_IN + CARD_HOLD + CARD_OUT) level = 3 - (int)((c - CARD_IN - CARD_HOLD) / (CARD_OUT / 4));
			if (level <= 0) { std::fill(top, top + n, 0xff000000u); return; }
			if (!cardMade) makeCard();
			if (level >= 4) std::copy(card.begin(), card.end(), top);
			else for (size_t i = 0; i < n; i++) top[i] = mul255(card[i], (uint32_t)(level * 64)) | 0xff000000u;
			return;
		}

		// both panels on their way to white, in eight steps: every colour drawn goes that far (C)
		{
			float wp = clamp01((t - (FOLLOW_END + (WHITE - FOLLOW_END) * 0.45)) / ((WHITE - FOLLOW_END) * 0.55));
			wl = (uint32_t)std::min(255, (int)(wp * 8) * 32);
		}
		// ---- the bottom panel: the zoom into the cartridge, its rumble, then the console left behind ----
		const float zp = smooth(clamp01(t / ZOOM_END));
		const float z = 1 + (ZOOM - 1) * zp;
		float cx = X(ANCHOR_X + (320 - ANCHOR_X) * zp), cy = X(ANCHOR_Y + (240 - ANCHOR_Y) * zp);
		if (t >= ZOOM_END && t < LAUNCH + 200)
		{
			// the rumble: whole look-pixels left and right, harder towards the burst
			static const int shake[8] = { 0, 2, -2, 1, -1, 2, -2, 0 };
			int i = (int)(t / 40) % 8, amp = t < RUMBLE_END - 150 ? 1 : 2;
			cx += shake[i] * amp * u;
			cy += (i & 1 ? u : 0);
		}
		const float camDrop = (float)travel(t) * 0.85f;	// the console moves off as fast as the nearest stars
		const int dy = X(camDrop);
		if (dy < H) zoomed(bot, startBot, X(ANCHOR_X), X(ANCHOR_Y), cx, cy, z, dy);
		else std::fill(bot, bot + n, C(SKY));
		if (dy > 0) sky(bot, 1, t);	// (above the console as it goes)
		// the light out of the cartridge's top as it rumbles, then the bananas
		if (t >= ZOOM_END - 100 && t < LAUNCH + 400 && dy < H)
		{
			float p = clamp01((t - (ZOOM_END - 100)) / (LAUNCH - ZOOM_END + 100));
			bool flick = ((int)(t / 50)) % 2 == 0;
			int h = (int)std::round(p * 6 + (flick ? 1 : 0)) * u;
			int yTop = X(CART_TOP) + dy;
			uint32_t c = mul255(rnds::tintAccent(0xfffff4aau), t < LAUNCH ? 140 + (flick ? 60 : 0) : 220);
			rect(bot, X(320 - 180), yTop - h, X(360), h, c);
			if (t >= LAUNCH) rect(bot, X(320 - 150), yTop - h - 3 * u, X(300), 3 * u, mul255(0xffffffffu, 200));
		}

		// ---- the top panel: the boot animation's picture going dark, the sky ----
		{
			int step = t < 500 ? (int)(t / 125) : 4;	// four steps to dark (made in setStart), the stars over them
			if (step == 0) std::copy(startTop.begin(), startTop.end(), top);
			else if (step < 4) std::copy(dim[step - 1].begin(), dim[step - 1].end(), top);
			else std::fill(top, top + n, C(SKY));
			if (step > 0) sky(top, 0, t);
		}

		// ---- the bananas ----
		const Fruit& lead = fruit[0];
		float lx = lead.x0, ly;
		const float originY = BOT_Y + CART_TOP;
		{
			float p = clamp01((t - lead.launch) / (LOCK - lead.launch));
			float e = 1 - (1 - p) * (1 - p) * (1 - p);
			ly = originY + (LOCK_Y - originY) * e;
			if (t >= LOCK) ly = LOCK_Y + 2 * std::round(std::sin((t - LOCK) * 0.006));	// a bob, in whole look-pixels
		}
		// the white in the glow: the lead first, the others after, in steps
		float gp = clamp01((t - FOLLOW_END) / (WHITE - FOLLOW_END));
		int leadWhite = std::min(4, (int)(gp * 2.0f * 4)), restWhite = std::min(4, (int)(std::max(0.0f, gp - 0.3f) * 1.6f * 4));
		halo(top, bot, lx, ly, t);
		// the ones behind it first, the smallest (furthest) first
		for (size_t i = fruit.size() - 1; i >= 1; i--)
		{
			const Fruit& f = fruit[i];
			if (t < f.launch) continue;
			float tau = (float)(t - f.launch) / 1000.0f;
			float bx = f.x0 + f.vx * tau, by = originY - f.vy * tau + 0.5f * 900 * tau * tau;
			float sx = f.slotX + f.sway * std::sin((float)t * f.swayRate + f.phase), sy = f.slotY + 6 * std::sin((float)t * f.swayRate * 1.3f + f.phase);
			float b = smooth(clamp01((t - f.launch - 450) / 700));
			float x = bx + (sx - bx) * b, y = by + (sy - by) * b;
			int turn = f.spin ? (int)std::floor((t - f.launch) / 1000.0 * std::abs(f.spin)) * (f.spin > 0 ? 1 : 3) : 0;
			banana(top, bot, std::round(x / 2) * 2, std::round(y / 2) * 2, f.k, turn, restWhite);
		}
		if (t >= lead.launch) banana(top, bot, lx, ly, lead.k, 0, leadWhite);
		if (!burstCued && t >= LAUNCH) { burstCued = true; if (onCue) onCue("burst"); }

	}
}
