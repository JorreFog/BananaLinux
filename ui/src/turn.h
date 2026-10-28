// The panel's rotation, for the composed screen's present (kms.cpp). Kept apart from Display so the tiled transpose
// can be unit-tested without a DRM device (ui/tests/turn-test.cpp): QEMU's virtio panel reports no orientation, so
// the boot test never turns anything, and the handheld's panel is mounted at 90 degrees.
#pragma once
#include <cstdint>
#include <cstring>

namespace banana
{
	// turnRect: the part of the composed screen (W x H, rows W apart) that changed -- [x0, x1) x [y0, y1) -- into a
	// scanout buffer `map` whose rows are `pitch` bytes apart, turned by rot (90, 180 or 270 clockwise). A turned
	// rectangle is written a panel row at a time (contiguous writes, strided reads of the cached screen).
#if defined(__SSE2__) || defined(__ARM_NEON) || defined(__aarch64__)
	typedef uint32_t V4u32 __attribute__((vector_size(16)));
	// the 4x4 transpose: a, b, c, d are rows, they come back as columns
	static inline void transpose4(V4u32& a, V4u32& b, V4u32& c, V4u32& d)
	{
		V4u32 t0 = __builtin_shuffle(a, b, (V4u32){ 0, 4, 1, 5 });
		V4u32 t1 = __builtin_shuffle(a, b, (V4u32){ 2, 6, 3, 7 });
		V4u32 t2 = __builtin_shuffle(c, d, (V4u32){ 0, 4, 1, 5 });
		V4u32 t3 = __builtin_shuffle(c, d, (V4u32){ 2, 6, 3, 7 });
		a = __builtin_shuffle(t0, t2, (V4u32){ 0, 1, 4, 5 });
		b = __builtin_shuffle(t0, t2, (V4u32){ 2, 3, 6, 7 });
		c = __builtin_shuffle(t1, t3, (V4u32){ 0, 1, 4, 5 });
		d = __builtin_shuffle(t1, t3, (V4u32){ 2, 3, 6, 7 });
	}
#endif

	static inline void turnRect(const uint32_t* src, uint8_t* map, size_t pitch, int W, int H, int rot, int x0, int y0, int x1, int y1)
	{
		int x = x0, y;
		if (rot == 90)
		{
			// a quarter turn clockwise: the screen's top row becomes the panel's right column
#if defined(__SSE2__) || defined(__ARM_NEON) || defined(__aarch64__)
			for (; x + 4 <= x1; x += 4)
			{
				for (y = y0; y + 4 <= y1; y += 4)
				{
					V4u32 r0, r1, r2, r3;
					memcpy(&r0, src + (size_t)(y + 0) * W + x, sizeof r0);
					memcpy(&r1, src + (size_t)(y + 1) * W + x, sizeof r1);
					memcpy(&r2, src + (size_t)(y + 2) * W + x, sizeof r2);
					memcpy(&r3, src + (size_t)(y + 3) * W + x, sizeof r3);
					transpose4(r0, r1, r2, r3);	// r0..r3: the columns x..x+3, top to bottom
					const V4u32 rev = { 3, 2, 1, 0 };
					V4u32 c0 = __builtin_shuffle(r0, rev), c1 = __builtin_shuffle(r1, rev), c2 = __builtin_shuffle(r2, rev), c3 = __builtin_shuffle(r3, rev);
					// panel row x+i, columns H-4-y..H-1-y: the block's four rows bottom to top
					memcpy((uint32_t*)(map + (size_t)(x + 0) * pitch) + (H - 4 - y), &c0, sizeof c0);
					memcpy((uint32_t*)(map + (size_t)(x + 1) * pitch) + (H - 4 - y), &c1, sizeof c1);
					memcpy((uint32_t*)(map + (size_t)(x + 2) * pitch) + (H - 4 - y), &c2, sizeof c2);
					memcpy((uint32_t*)(map + (size_t)(x + 3) * pitch) + (H - 4 - y), &c3, sizeof c3);
				}
				for (; y < y1; y++)	// the rows the 4x4 blocks left
					for (int i = 0; i < 4; i++) ((uint32_t*)(map + (size_t)(x + i) * pitch))[H - 1 - y] = src[(size_t)y * W + x + i];
			}
#endif
			for (; x < x1; x++)
			{
				uint32_t* row = (uint32_t*)(map + (size_t)x * pitch);
				for (y = y0; y < y1; y++) row[H - 1 - y] = src[(size_t)y * W + x];
			}
		}
		else if (rot == 270)
		{
			// the other way: the screen's top row becomes the panel's left column, and the screen's left column the
			// panel's bottom row (a mirror would be row = x, col = y -- the text would read backwards)
#if defined(__SSE2__) || defined(__ARM_NEON) || defined(__aarch64__)
			for (; x + 4 <= x1; x += 4)
			{
				for (y = y0; y + 4 <= y1; y += 4)
				{
					V4u32 r0, r1, r2, r3;
					memcpy(&r0, src + (size_t)(y + 0) * W + x, sizeof r0);
					memcpy(&r1, src + (size_t)(y + 1) * W + x, sizeof r1);
					memcpy(&r2, src + (size_t)(y + 2) * W + x, sizeof r2);
					memcpy(&r3, src + (size_t)(y + 3) * W + x, sizeof r3);
					transpose4(r0, r1, r2, r3);	// r0..r3: the columns x..x+3, top to bottom
					// panel row W-1-(x+i), columns y..y+3
					memcpy((uint32_t*)(map + (size_t)(W - 1 - (x + 0)) * pitch) + y, &r0, sizeof r0);
					memcpy((uint32_t*)(map + (size_t)(W - 1 - (x + 1)) * pitch) + y, &r1, sizeof r1);
					memcpy((uint32_t*)(map + (size_t)(W - 1 - (x + 2)) * pitch) + y, &r2, sizeof r2);
					memcpy((uint32_t*)(map + (size_t)(W - 1 - (x + 3)) * pitch) + y, &r3, sizeof r3);
				}
				for (; y < y1; y++)
					for (int i = 0; i < 4; i++) ((uint32_t*)(map + (size_t)(W - 1 - (x + i)) * pitch))[y] = src[(size_t)y * W + x + i];
			}
#endif
			for (; x < x1; x++)
			{
				uint32_t* row = (uint32_t*)(map + (size_t)(W - 1 - x) * pitch);
				for (y = y0; y < y1; y++) row[y] = src[(size_t)y * W + x];
			}
		}
		else	// 180: the panel's page is the screen's, backwards
		{
			for (y = y0; y < y1; y++)
			{
				uint32_t* row = (uint32_t*)(map + (size_t)(H - 1 - y) * pitch);
				for (x = x0; x < x1; x++) row[W - 1 - x] = src[(size_t)y * W + x];
			}
		}
	}
}
