// the quick menu's sheet (src/sheet.h): its way up and down, and the frames composed without a plane
#include <cstdio>
#include <vector>
#include "sheet.h"
using namespace banana;
static int failed = 0;
#define CHECK(c, what) do { if (!(c)) { fprintf(stderr, "FAIL sheet: %s\n", what); failed = 1; } } while (0)
int main()
{
	CHECK(sheetUpAt(0) == 0 && sheetUpAt(1) == 1, "up: from below the panel to its place");
	CHECK(sheetDownAt(0) == 1 && sheetDownAt(1) == 0, "down: from its place to below the panel");
	float top = 0, low = 1, hop = 0;
	bool passed = false;
	for (int i = 0; i <= 1000; i++)
	{
		const float u = sheetUpAt(i / 1000.0), d = sheetDownAt(i / 1000.0);
		if (u > top) top = u;
		if (u > 1) passed = true;
		if (passed && u < low) low = u;
		if (d > hop) hop = d;
		CHECK(d >= 0 && u >= 0, "never below nothing");
	}
	CHECK(top > 1.02f && top < 1.07f, "up: a bounce past its place, a small one");
	CHECK(low > 0.995f, "up: no second dip one could see");
	CHECK(1 - sheetUpAt(0.999) < 0.002f, "up: no jump at the end");
	CHECK(hop > 1.01f && hop < 1.06f, "down: a hop before the drop");
	// frames: 4 wide, 10 tall; under is rows of 100+y, the sheet rows of 200+y
	const int w = 4, h = 10;
	std::vector<uint32_t> under(w * h), sheet(w * h), out(w * h, 7);
	for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) { under[y * w + x] = 100 + y; sheet[y * w + x] = 200 + y; }
	sheetFrame(under.data(), sheet.data(), w, h, 0, out.data());
	CHECK(out == under, "nothing shown: what is under it");
	sheetFrame(under.data(), sheet.data(), w, h, 1, out.data());
	CHECK(out == sheet, "in its place: the sheet");
	sheetFrame(under.data(), sheet.data(), w, h, 0.3f, out.data());
	CHECK(out[6 * w] == 106 && out[7 * w] == 200 && out[9 * w + 3] == 202, "three rows up: its first rows at the panel's foot");
	sheetFrame(under.data(), sheet.data(), w, h, 1.2f, out.data());
	CHECK(out[0] == 202 && out[7 * w] == 209 && out[8 * w] == 108 && out[9 * w] == 109, "past its place: its top rows cut, under it below");
	sheetFrame(under.data(), sheet.data(), w, h, 5, out.data());
	CHECK(out == under, "far past: never outside the buffers");
	if (!failed) printf("sheet: all checks passed\n");
	return failed;
}
