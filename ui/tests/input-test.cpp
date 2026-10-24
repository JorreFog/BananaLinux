// the left stick as a d-pad (input.cpp's stickStep), on the RG DS joypad's range: -1800..1800, at rest 0
#include "input.h"
#include <cstdio>
using banana::stickStep;
static int fails;
static void expect(int got, int want, const char* what) { if (got != want) { fprintf(stderr, "FAIL %s: %d, not %d\n", what, got, want); fails++; } }
int main()
{
	const int H = 1800;
	expect(stickStep(0, 0, H), 0, "at rest");
	expect(stickStep(0, 300, H), 0, "inside the deadzone");
	expect(stickStep(0, 899, H), 0, "just under half way");
	expect(stickStep(0, 1000, H), 1, "past half way: right/down");
	expect(stickStep(0, -1000, H), -1, "past half way: left/up");
	expect(stickStep(1, 700, H), 1, "coming back, over a third: still held");
	expect(stickStep(1, 500, H), 0, "under a third: let go");
	expect(stickStep(1, -1500, H), -1, "flicked to the other side");
	expect(stickStep(-1, -200, H), 0, "back to the middle");
	if (!fails) fprintf(stderr, "input-test: all passed\n");
	return fails ? 1 : 0;
}
