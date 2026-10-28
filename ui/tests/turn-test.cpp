// turnRect's unit test: the tiled SIMD transpose must write exactly what the scalar loop does, for any rectangle of
// any size (the panel's own 1240x1080, odd sizes for the edges the 4x4 blocks leave, and a pitch that isn't tight).
// QEMU's virtio panel reports no orientation, so the boot test never turns anything -- this is what covers the
// handheld's 90-degree panel. Built and run by ui/tests/run.sh.
#include "../src/turn.h"
#include <cstdio>
#include <cstdint>
#include <random>
#include <tuple>
#include <vector>

using banana::turnRect;

// the scalar reference: the same mapping, one pixel at a time
static void turnScalar(const uint32_t* src, uint8_t* map, size_t pitch, int W, int H, int rot, int x0, int y0, int x1, int y1)
{
	if (rot == 90)
		for (int x = x0; x < x1; x++) { uint32_t* row = (uint32_t*)(map + (size_t)x * pitch); for (int y = y0; y < y1; y++) row[H - 1 - y] = src[(size_t)y * W + x]; }
	else if (rot == 270)
		for (int x = x0; x < x1; x++) { uint32_t* row = (uint32_t*)(map + (size_t)(W - 1 - x) * pitch); for (int y = y0; y < y1; y++) row[y] = src[(size_t)y * W + x]; }
	else
		for (int y = y0; y < y1; y++) { uint32_t* row = (uint32_t*)(map + (size_t)(H - 1 - y) * pitch); for (int x = x0; x < x1; x++) row[W - 1 - x] = src[(size_t)y * W + x]; }
}

int main()
{
	std::mt19937 rng(5);
	int bad = 0;
	// the panel's own sizes (SW x SH), and odd ones so the 4x4 blocks always leave a remainder
	for (auto [SW, SH] : { std::tuple{ 1240, 1080 }, std::tuple{ 21, 25 } })
	{
		size_t pitch = (size_t)SW * 4 + 16;	// not tight: the panel's rows are aligned
		for (int rot : { 90, 180, 270 })
		{
			// the composed screen's size for this rotation (as Display::openSingle sets it)
			int W = rot == 90 || rot == 270 ? SH : SW, H = rot == 90 || rot == 270 ? SW : SH;
			std::vector<uint32_t> src((size_t)W * H);
			int badHere = 0;
			for (int trial = 0; trial < 4000; trial++)
			{
				for (auto& v : src) v = rng();
				int x0 = rng() % W, x1 = x0 + 1 + rng() % (W - x0);
				int y0 = rng() % H, y1 = y0 + 1 + rng() % (H - y0);
				std::vector<uint8_t> a((size_t)SH * pitch, 0), b((size_t)SH * pitch, 0);
				turnScalar(src.data(), a.data(), pitch, W, H, rot, x0, y0, x1, y1);
				turnRect(src.data(), b.data(), pitch, W, H, rot, x0, y0, x1, y1);
				if (a != b) { badHere++; if (badHere == 1) printf("panel %dx%d rot %d: mismatch at x[%d,%d) y[%d,%d)\n", SW, SH, rot, x0, x1, y0, y1); }
			}
			printf("panel %dx%d rot %d: %s\n", SW, SH, rot, badHere ? "MISMATCH" : "ok");
			bad += badHere;
		}
	}
	printf("turnRect: %s\n", bad ? "FAILED" : "all rectangles match the scalar loop");
	return bad != 0;
}
