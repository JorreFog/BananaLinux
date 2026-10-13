#include "cart3d.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace banana
{
	namespace
	{
		// ---- the card, in millimetres: x right, y up, z out of the front; centred ------------------------------------
		const float HX = 17.5f, HY = 16.5f, HZ = 1.9f;	// half its width, height and thickness
		const float CORNER = 1.1f;						// the outline's corner radius
		const float NOTCH = 4.4f;						// the top-right corner cut off, along each edge
		const float FILLET = 0.45f;						// the edges' rounding
		// the label well: its rectangle (centre and half size), depth and corner radius
		const float LCX = 0, LCY = -2.0f, LHX = 14.6f, LHY = 12.6f, LDEPTH = 0.22f, LR = 1.0f;
		const float BAND = 4.2f;						// the light band across the label's top (mm of its height)

		struct V3 { float x, y, z; };
		inline V3 operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
		inline V3 operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
		inline V3 operator*(V3 a, float k) { return { a.x * k, a.y * k, a.z * k }; }
		inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
		inline V3 norm(V3 a) { float l = std::sqrt(dot(a, a)); return l > 0 ? a * (1 / l) : a; }
		inline float len2(float x, float y) { return std::sqrt(x * x + y * y); }

		// a rounded rectangle's signed distance (half size b, corner radius r)
		inline float sdRoundRect(float px, float py, float bx, float by, float r)
		{
			float qx = std::fabs(px) - bx + r, qy = std::fabs(py) - by + r;
			return std::min(std::max(qx, qy), 0.0f) + len2(std::max(qx, 0.0f), std::max(qy, 0.0f)) - r;
		}
		// the card's outline in the plane, shrunk by g (the fillet: shrink, then round by as much)
		inline float outline2(float x, float y, float g)
		{
			float d = sdRoundRect(x, y, HX - g, HY - g, std::max(0.05f, CORNER - g));
			// the notch: a 45 degree cut through the top-right corner, through (HX - NOTCH, HY) and (HX, HY - NOTCH)
			const float k = 0.70710678f;
			float cut = ((x - (HX - NOTCH)) + (y - HY)) * k + g;
			return std::max(d, cut);
		}
		inline float sdBox(V3 p, V3 b)
		{
			float qx = std::fabs(p.x) - b.x, qy = std::fabs(p.y) - b.y, qz = std::fabs(p.z) - b.z;
			float out = std::sqrt(std::max(qx, 0.0f) * std::max(qx, 0.0f) + std::max(qy, 0.0f) * std::max(qy, 0.0f) + std::max(qz, 0.0f) * std::max(qz, 0.0f));
			return out + std::min(std::max(qx, std::max(qy, qz)), 0.0f);
		}

		// the card's signed distance
		float sdf(V3 p)
		{
			// the body: the outline extruded, its edges filleted
			float wx = outline2(p.x, p.y, FILLET), wy = std::fabs(p.z) - (HZ - FILLET);
			float body = std::min(std::max(wx, wy), 0.0f) + len2(std::max(wx, 0.0f), std::max(wy, 0.0f)) - FILLET;
			float d = body;
			// the seam all round where the two shells meet: a fine groove at mid-thickness
			float seam = std::max(std::fabs(p.z) - 0.13f, -body - 0.12f);
			d = std::max(d, -seam);
			// the notch in the left side for the slot's spring
			float spring = sdBox(p - V3{ -HX, 7.2f, 0 }, { 0.75f, 1.5f, 2.5f }) - 0.15f;
			d = std::max(d, -spring);
			// the label well: down into the front, its lip bevelled
			float wellXY = sdRoundRect(p.x - LCX, p.y - LCY, LHX, LHY, LR);
			float floorZ = HZ - LDEPTH;
			float well = std::max(wellXY + std::max(0.0f, p.z - floorZ) * 0.9f, floorZ - p.z);
			d = std::max(d, -well);
			// the ridges and the arrow only in a box over the front's top: wherever it is further than the card's own
			// distance they can't be nearer, and most samples skip them (the box is generous: the arrow's field is a bound
			// below its distance, and with this margin every picture comes out the same to the byte)
			if (sdBox(p - V3{ -0.6f, 12.95f, HZ }, { 13.0f, 2.0f, 0.6f }) >= d) return d;
			// three grip ridges moulded above the label
			for (int i = 0; i < 3; i++)
			{
				float ry = LCY + LHY + 1.4f + i * 0.85f;
				float ridge = sdBox(p - V3{ -2.2f, ry, HZ }, { 10.5f, 0.12f, 0.1f }) - 0.1f;
				d = std::min(d, ridge);
			}
			// the insertion arrow, moulded right of them: a small raised triangle pointing up (into the slot)
			{
				float ax = p.x - 10.6f, ay = p.y - (LCY + LHY + 2.25f);
				const float k = 0.894427f, h = 0.447214f;	// the sides' normals: (+-2, 1) / sqrt 5
				float tri = std::max({ ax * k + ay * h - 0.55f, -ax * k + ay * h - 0.55f, -ay - 0.85f });
				float arrow = std::max(tri - 0.06f, std::fabs(p.z - HZ) - 0.07f) - 0.03f;
				d = std::min(d, arrow);
			}
			return d;
		}
		V3 normalAt(V3 p)
		{
			const float e = 0.008f;
			return norm({ sdf(p + V3{ e, 0, 0 }) - sdf(p - V3{ e, 0, 0 }), sdf(p + V3{ 0, e, 0 }) - sdf(p - V3{ 0, e, 0 }), sdf(p + V3{ 0, 0, e }) - sdf(p - V3{ 0, 0, e }) });
		}

		// ---- the geometry buffer: one pose's card, at twice the picture's size ----------------------------------------
		struct GBuf
		{
			int W = 0, H = 0;
			std::vector<uint8_t> mat;		// 0 nothing, 1 the shell, 2 the label
			std::vector<float> u, v;		// on the label: where (0..1 across, 0..1 down)
			std::vector<float> dif, spec;	// the light it gets (multiplies its colour) and the shine (added, white; may pass 1)
		};
		GBuf build(const CartPose& pose, int SS)
		{
			float yaw = pose.yaw * (float)M_PI / 180, pitch = pose.pitch * (float)M_PI / 180;
			float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
			auto rot = [&](V3 v) -> V3	// object to camera: yaw (about y), then pitch (about x)
			{
				V3 a = { v.x * cy + v.z * sy, v.y, -v.x * sy + v.z * cy };
				return { a.x, a.y * cp - a.z * sp, a.y * sp + a.z * cp };
			};
			auto unrot = [&](V3 v) -> V3	// camera to object
			{
				V3 a = { v.x, v.y * cp + v.z * sp, -v.y * sp + v.z * cp };
				return { a.x * cy - a.z * sy, a.y, a.x * sy + a.z * cy };
			};
			const float D = 3.4f * 2 * HY;	// the camera's distance
			// the picture's extent: the card's box projected (screen y down)
			float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
			for (int i = 0; i < 8; i++)
			{
				V3 c = rot({ (i & 1) ? HX : -HX, (i & 2) ? HY : -HY, (i & 4) ? HZ : -HZ });
				float k = D / (D - c.z), sx = c.x * k, syy = -c.y * k;
				minx = std::min(minx, sx); maxx = std::max(maxx, sx); miny = std::min(miny, syy); maxy = std::max(maxy, syy);
			}
			float m = 0.03f * (maxy - miny);	// (room for the highlights' bloom: CART_MARGIN of the picture)
			minx -= m; maxx += m; miny -= m; maxy += m;
			GBuf g;
			g.H = pose.height * SS;
			float scale = g.H / (maxy - miny);
			g.W = std::max(1, (int)std::ceil((maxx - minx) * scale));
			size_t n = (size_t)g.W * g.H;
			g.mat.assign(n, 0); g.u.assign(n, 0); g.v.assign(n, 0); g.dif.assign(n, 0); g.spec.assign(n, 0);
			const V3 eye = unrot({ 0, 0, D });
			// the lights, in object space: a key from the top left in front, a fill from the right, a rim from behind
			const V3 key = unrot(norm({ -0.55f, 0.75f, 0.55f })), fill = unrot(norm({ 0.8f, 0.15f, 0.45f })), rim = unrot(norm({ 0.2f, 0.6f, -0.75f }));
			const float bx[3] = { HX + 0.3f, HY + 0.3f, HZ + 0.4f };
			// rows shared out over the cores (every pixel is its own: it runs once per pose, but for a second or more on
			// one of the RG DS's cores, and the cartridges wait for it)
			auto rows = [&](int y0, int y1)
			{
				for (int y = y0; y < y1; y++)
					for (int x = 0; x < g.W; x++)
					{
						float sx = (x + 0.5f) / scale + minx, syy = (y + 0.5f) / scale + miny;
						V3 dir = unrot(norm({ sx / D, -syy / D, -1 }));
						// the ray into the card's box (slabs), then sphere tracing
						float t0 = 0, t1 = 1e9f;
						const float o[3] = { eye.x, eye.y, eye.z }, dd[3] = { dir.x, dir.y, dir.z };
						bool miss = false;
						for (int a = 0; a < 3; a++)
						{
							if (std::fabs(dd[a]) < 1e-7f) { if (std::fabs(o[a]) > bx[a]) miss = true; continue; }
							float ta = (-bx[a] - o[a]) / dd[a], tb = (bx[a] - o[a]) / dd[a];
							if (ta > tb) std::swap(ta, tb);
							t0 = std::max(t0, ta); t1 = std::min(t1, tb);
						}
						if (miss || t0 > t1) continue;
						float t = t0;
						bool hit = false;
						V3 p{};
						for (int i = 0; i < 96 && t <= t1; i++)
						{
							p = eye + dir * t;
							float d = sdf(p);
							if (d < 0.002f) { hit = true; break; }
							t += d * 0.95f;
						}
						if (!hit) continue;
						size_t at = (size_t)y * g.W + x;
						V3 nrm = normalAt(p);
						V3 view = norm(eye - p);
						// ambient occlusion: how open the space is along the normal
						float occ = 0;
						for (int k = 1; k <= 4; k++) { float h = 0.08f * k; occ += (h - std::max(0.0f, sdf(p + nrm * h))) / h * (0.5f / k); }
						float ao = std::min(1.0f, std::max(0.35f, 1 - occ));
						bool label = std::fabs(p.z - (HZ - LDEPTH)) < 0.02f && sdRoundRect(p.x - LCX, p.y - LCY, LHX - 0.05f, LHY - 0.05f, LR) < 0 && nrm.z > 0.98f;
						float shin = label ? 90.0f : 42.0f, ks = label ? 0.55f : 0.32f;
						auto blinn = [&](V3 L, float i) { V3 h = norm(L + view); return i * ks * std::pow(std::max(0.0f, dot(nrm, h)), shin); };
						float dif = 0.26f * ao + 0.82f * std::max(0.0f, dot(nrm, key)) + 0.28f * std::max(0.0f, dot(nrm, fill)) + 0.12f * std::max(0.0f, dot(nrm, rim));
						float spec = blinn(key, 1.0f) + blinn(fill, 0.35f);
						// the studio it stands in, reflected: a big softbox overhead behind it (the top edge's bright rim), a smaller one
						// in front, a strip light on the left, a dim room;
						// sharper on the laminated label than on the satin plastic, stronger at grazing angles (Fresnel)
						V3 R = rot(nrm * (2 * dot(nrm, view)) - view);	// (in camera space)
						float soft = label ? 0.03f : 0.09f;
						auto boxLight = [&](V3 c, float size) { float a = dot(R, norm(c)); return std::min(1.0f, std::max(0.0f, (a - (1 - size)) / soft + 0.5f)); };
						float env = 1.7f * boxLight({ -0.2f, 0.85f, -0.4f }, 0.12f) + 0.6f * boxLight({ 0.1f, 0.7f, 0.7f }, 0.06f) + 0.9f * boxLight({ -0.95f, 0.15f, 0.3f }, 0.05f) + 0.05f + 0.04f * R.y;
						float cosv = std::max(0.0f, dot(nrm, view)), f0 = label ? 0.05f : 0.04f;
						float fres = f0 + (1 - f0) * std::pow(1 - cosv, 5);
						spec += env * fres * (label ? 1.0f : 0.8f) * ao;
						g.mat[at] = label ? 2 : 1;
						g.dif[at] = label ? std::min(1.25f, dif * 1.2f + 0.06f) : dif * ao;	// (the paper label brighter than the plastic)
						g.spec[at] = spec;
						if (label)
						{
							g.u[at] = (p.x - (LCX - LHX)) / (2 * LHX);
							g.v[at] = ((LCY + LHY) - p.y) / (2 * LHY);
						}
					}
			};
			int nt = std::max(1, std::min(4, (int)std::thread::hardware_concurrency()));
			std::vector<std::thread> ts;
			for (int i = 1; i < nt; i++) ts.emplace_back(rows, g.H * i / nt, g.H * (i + 1) / nt);
			rows(0, g.H / nt);
			for (auto& t : ts) t.join();
			return g;
		}
		// built once per pose and kept (a few hundred milliseconds each on the RG DS's cores)
		std::mutex gm;
		std::map<std::string, std::shared_ptr<GBuf>> gbufs;
		std::shared_ptr<GBuf> gbufFor(const CartPose& pose, int SS)
		{
			std::string key = std::to_string((int)std::lround(pose.yaw * 10)) + "|" + std::to_string((int)std::lround(pose.pitch * 10)) + "|" + std::to_string(pose.height);
			{
				std::lock_guard<std::mutex> l(gm);
				auto it = gbufs.find(key);
				if (it != gbufs.end()) return it->second;
			}
			auto g = std::make_shared<GBuf>(build(pose, SS));
			std::lock_guard<std::mutex> l(gm);
			if (gbufs.size() > 8) gbufs.clear();
			gbufs[key] = g;
			return g;
		}

		uint32_t shellOf(const std::string& s)
		{
			if (s == "nds") return 0x35363b;
			if (s == "gba") return 0x4a4860;
			if (s == "gb") return 0xa6a7a2;
			if (s == "gbc") return 0x3d3e48;
			if (s == "n64") return 0x5a5d63;
			if (s == "snes") return 0x9b9ea4;
			if (s == "nes") return 0x8b8c91;
			if (s == "megadrive") return 0x1e1f23;
			if (s == "psx") return 0x2b2c31;
			return 0x3b3c42;
		}
	}

	float cartAspect(const std::string&) { return HX / HY; }

	void cartLabelSize(float& w, float& bandH, float& artH)
	{
		w = 2 * LHX; bandH = BAND; artH = 2 * LHY - BAND;
	}

	rnds::Bitmap renderCart(const std::string& system, const rnds::Bitmap& label, const CartPose& pose, const rnds::Bitmap& icon, const rnds::Bitmap& bandText)
	{
		const int SS = 2;
		std::shared_ptr<GBuf> gp = gbufFor(pose, SS);
		const GBuf& g = *gp;
		// the label as a picture: the light band at its top (the system's name), the art covered into the rest
		const float bandFrac = BAND / (2 * LHY);
		int LW = std::max(16, (int)std::lround(g.H * (2 * LHX) / (2 * HY))), LH = std::max(16, (int)std::lround(g.H * (2 * LHY) / (2 * HY)));
		int bandPx = (int)std::lround(LH * bandFrac);
		std::vector<uint8_t> lab((size_t)LW * LH * 3);
		auto put = [&](int x, int y, uint32_t c) { uint8_t* p = &lab[((size_t)y * LW + x) * 3]; p[0] = (uint8_t)(c >> 16); p[1] = (uint8_t)(c >> 8); p[2] = (uint8_t)c; };
		for (int y = 0; y < bandPx; y++) for (int x = 0; x < LW; x++) put(x, y, y >= bandPx - 1 ? 0xb9bbc0 : 0xecedf0);
		if (!bandText.empty() && bandText.alpha)
		{
			// the band's text, scaled to the band's height and centred, in a dark grey
			float k = std::min(1.0f, std::min((bandPx * 0.55f) / bandText.h, (LW * 0.8f) / bandText.w));
			int tw = std::max(1, (int)(bandText.w * k)), th = std::max(1, (int)(bandText.h * k));
			rnds::Bitmap t = k < 1 ? rnds::resize(bandText, tw, th) : bandText;
			int ox = (LW - t.w) / 2, oy = (bandPx - t.h) / 2;
			for (int y = 0; y < t.h; y++)
				for (int x = 0; x < t.w; x++)
				{
					int X = ox + x, Y = oy + y;
					if (X < 0 || Y < 0 || X >= LW || Y >= bandPx) continue;
					float a = t.px[(size_t)y * t.w + x] / 255.0f;
					uint8_t* p = &lab[((size_t)Y * LW + X) * 3];
					for (int c = 0; c < 3; c++) p[c] = (uint8_t)(p[c] * (1 - a) + 0x3a * a);
				}
		}
		int artH = LH - bandPx;
		rnds::Bitmap art = label.empty() || label.alpha ? rnds::Bitmap() : rnds::cover(label, LW, artH);
		for (int y = 0; y < artH; y++)
			for (int x = 0; x < LW; x++)
			{
				uint32_t c = 0xd8d9dc;
				if (!art.empty()) { const uint8_t* s = &art.px[((size_t)y * art.w + x) * 4]; c = (uint32_t)s[0] << 16 | (uint32_t)s[1] << 8 | s[2]; }
				put(x, bandPx + y, c);
			}
		if (art.empty() && !icon.empty() && !icon.alpha)
		{
			int s = std::min(LW, artH) / 2;
			rnds::Bitmap ic = rnds::resizeNearest(icon, s, s);
			int ox = (LW - s) / 2, oy = bandPx + (artH - s) / 2;
			for (int y = 0; y < s; y++)
				for (int x = 0; x < s; x++)
				{
					const uint8_t* p = &ic.px[((size_t)y * s + x) * 4];
					float a = p[3] / 255.0f;
					uint8_t* q = &lab[((size_t)(oy + y) * LW + ox + x) * 3];
					for (int c = 0; c < 3; c++) q[c] = (uint8_t)(q[c] * (1 - a) + p[c] * a);
				}
		}
		// the card: each buffer pixel lit (unclipped), then averaged down to the picture's size, premultiplied
		uint32_t shell = shellOf(system);
		float br = ((shell >> 16) & 255) / 255.0f, bg = ((shell >> 8) & 255) / 255.0f, bb = (shell & 255) / 255.0f;
		const int OW = g.W / SS, OH = g.H / SS;
		std::vector<float> hdr((size_t)OW * OH * 4, 0.0f);
		for (int y = 0; y < OH; y++)
			for (int x = 0; x < OW; x++)
			{
				float acc[4] = { 0, 0, 0, 0 };
				for (int j = 0; j < SS; j++)
					for (int i = 0; i < SS; i++)
					{
						size_t at = (size_t)(y * SS + j) * g.W + (x * SS + i);
						uint8_t mt = g.mat[at];
						if (!mt) continue;
						float r = br, gg = bg, b = bb;
						if (mt == 2)
						{
							float fu = std::min(std::max(g.u[at] * (LW - 1), 0.0f), (float)(LW - 1)), fv = std::min(std::max(g.v[at] * (LH - 1), 0.0f), (float)(LH - 1));
							int iu = (int)fu, iv = (int)fv, iu1 = std::min(iu + 1, LW - 1), iv1 = std::min(iv + 1, LH - 1);
							float du = fu - iu, dv = fv - iv, c[3];
							for (int k = 0; k < 3; k++)
								c[k] = ((lab[((size_t)iv * LW + iu) * 3 + k] * (1 - du) + lab[((size_t)iv * LW + iu1) * 3 + k] * du) * (1 - dv) +
									(lab[((size_t)iv1 * LW + iu) * 3 + k] * (1 - du) + lab[((size_t)iv1 * LW + iu1) * 3 + k] * du) * dv) / 255.0f;
							r = c[0]; gg = c[1]; b = c[2];
						}
						float d = g.dif[at], s = g.spec[at];
						acc[0] += r * d + s; acc[1] += gg * d + s; acc[2] += b * d + s; acc[3] += 1;
					}
				float* o = &hdr[((size_t)y * OW + x) * 4];
				for (int k = 0; k < 4; k++) o[k] = acc[k] / (SS * SS);
			}
		// post: the highlights bloom (what passes white, blurred twice with a box about 1.2% of the height, spills a little
		// light around them, past the card's edge too), then a soft shoulder instead of a hard clip at white
		{
			int rad = std::max(1, (int)std::lround(OH * 0.012f));
			std::vector<float> bl((size_t)OW * OH * 3, 0.0f), tmp(bl.size());
			bool any = false;
			for (size_t i = 0, n = (size_t)OW * OH; i < n; i++)
				for (int k = 0; k < 3; k++)
				{
					float v = std::max(0.0f, hdr[i * 4 + k] - 1.0f * hdr[i * 4 + 3]);
					bl[i * 3 + k] = v;
					any |= v > 0;
				}
			if (any)
			{
				auto blur = [&](std::vector<float>& src, std::vector<float>& dst, bool vertical)
				{
					int L = vertical ? OH : OW, M = vertical ? OW : OH;
					for (int m = 0; m < M; m++)
					{
						auto idx = [&](int l) { return (size_t)(vertical ? l * OW + m : m * OW + l) * 3; };
						float sum[3] = { 0, 0, 0 };
						for (int l = -rad; l <= rad; l++) if (l >= 0 && l < L) for (int k = 0; k < 3; k++) sum[k] += src[idx(l) + k];
						for (int l = 0; l < L; l++)
						{
							for (int k = 0; k < 3; k++) dst[idx(l) + k] = sum[k] / (2 * rad + 1);
							int out = l - rad, in = l + rad + 1;
							if (out >= 0) for (int k = 0; k < 3; k++) sum[k] -= src[idx(out) + k];
							if (in < L) for (int k = 0; k < 3; k++) sum[k] += src[idx(in) + k];
						}
					}
				};
				for (int pass = 0; pass < 2; pass++) { blur(bl, tmp, false); blur(tmp, bl, true); }
				for (size_t i = 0, n = (size_t)OW * OH; i < n; i++)
				{
					float add[3] = { bl[i * 3] * 0.7f, bl[i * 3 + 1] * 0.7f, bl[i * 3 + 2] * 0.7f };
					float a = hdr[i * 4 + 3];
					// outside the card the glow is a light veil: its own alpha, its colour its strength
					float glowA = std::min(1.0f, std::max({ add[0], add[1], add[2] }));
					float na = a + glowA * (1 - a);
					for (int k = 0; k < 3; k++) hdr[i * 4 + k] += add[k];
					hdr[i * 4 + 3] = na;
				}
			}
		}
		auto shoulder = [](float v) { return v < 0.8f ? v : 0.8f + 0.2f * (1 - std::exp(-(v - 0.8f) / 0.2f)); };
		rnds::Bitmap out;
		out.w = OW; out.h = OH;
		out.px.assign((size_t)OW * OH * 4, 0);
		for (size_t i = 0, n = (size_t)OW * OH; i < n; i++)
		{
			float a = hdr[i * 4 + 3];
			if (a < 1.0f / 255) continue;
			uint8_t* o = &out.px[i * 4];
			for (int k = 0; k < 3; k++) o[k] = (uint8_t)std::lround(std::min(1.0f, shoulder(hdr[i * 4 + k] / a)) * 255);
			o[3] = (uint8_t)std::lround(std::min(1.0f, a) * 255);
		}
		return out;
	}
}
