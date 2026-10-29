// Integer workloads compiled as Thumb-1 code (the Makefile builds this file with
// -mthumb -mfloat-abi=softfp; Thumb-1 cannot use the hard-float ABI). Many 3DS
// games ship mixed ARM/Thumb code, so the emulator's Thumb path gets exercised.
// No floating point and no library calls in this file (3dsxtool rejects the
// halfword-aligned R_ARM_THM_CALL relocations that calls into ARM libc produce).
#include "bench.h"

// Mark the object as compatible with the hard-float VFP calling convention
// (no float arguments cross this file's interface).
__asm__(".eabi_attribute 28, 3");

static u32 crcTable[256];

#define HASH_BITS 13
#define HASH_SIZE (1u << HASH_BITS)
static u32 hashKeys[HASH_SIZE];
static u32 hashVals[HASH_SIZE];

void thumb_init(void) {
	for (u32 i = 0; i < 256; i++) {
		u32 c = i;
		for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
		crcTable[i] = c;
	}
}

u32 thumb_crc32(const u8* p, u32 n, u32 crc) {
	crc = ~crc;
	while (n--) crc = crcTable[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
	return ~crc;
}

// Bottom-up merge sort of (key, value) pairs; returns a checksum of the order.
u32 thumb_sort(u32* keys, u32* vals, u32* tk, u32* tv, int n) {
	u32 *sk = keys, *sv = vals, *dk = tk, *dv = tv;
	// insertion sort runs of 8
	for (int base = 0; base < n; base += 8) {
		int end = base + 8 < n ? base + 8 : n;
		for (int i = base + 1; i < end; i++) {
			u32 k = sk[i], v = sv[i];
			int j = i - 1;
			while (j >= base && sk[j] > k) {
				sk[j + 1] = sk[j];
				sv[j + 1] = sv[j];
				j--;
			}
			sk[j + 1] = k;
			sv[j + 1] = v;
		}
	}
	for (int width = 8; width < n; width <<= 1) {
		for (int lo = 0; lo < n; lo += 2 * width) {
			int mid = lo + width < n ? lo + width : n;
			int hi = lo + 2 * width < n ? lo + 2 * width : n;
			int a = lo, b = mid, o = lo;
			while (a < mid && b < hi) {
				if (sk[a] <= sk[b]) { dk[o] = sk[a]; dv[o++] = sv[a++]; }
				else                { dk[o] = sk[b]; dv[o++] = sv[b++]; }
			}
			while (a < mid) { dk[o] = sk[a]; dv[o++] = sv[a++]; }
			while (b < hi)  { dk[o] = sk[b]; dv[o++] = sv[b++]; }
		}
		u32* t;
		t = sk; sk = dk; dk = t;
		t = sv; sv = dv; dv = t;
	}
	if (sk != keys) {
		for (int i = 0; i < n; i++) {
			keys[i] = sk[i];
			vals[i] = sv[i];
		}
	}
	u32 h = 0;
	for (int i = 0; i < n; i += 16) h = h * 31 + vals[i];
	return h;
}

static inline u32 mix(u32 x) {
	x ^= x >> 16;
	x *= 0x7FEB352Du;
	x ^= x >> 15;
	x *= 0x846CA68Bu;
	x ^= x >> 16;
	return x;
}

// Open-addressing hash table: insert n keys, then look up n keys (~50% hits).
u32 thumb_hash_bench(u32 seed, int n) {
	if (n > (int)(HASH_SIZE / 2)) n = HASH_SIZE / 2;
	for (u32 i = 0; i < HASH_SIZE; i++) hashKeys[i] = 0;
	u32 s = seed | 1;
	for (int i = 0; i < n; i++) {
		u32 key = mix(s + (u32)i) | 1;
		u32 slot = key & (HASH_SIZE - 1);
		while (hashKeys[slot] && hashKeys[slot] != key) slot = (slot + 1) & (HASH_SIZE - 1);
		hashKeys[slot] = key;
		hashVals[slot] = (u32)i;
	}
	u32 hits = 0, acc = 0;
	for (int i = 0; i < n; i++) {
		u32 key = mix(s + (u32)(i * 2)) | 1;
		u32 slot = key & (HASH_SIZE - 1);
		while (hashKeys[slot]) {
			if (hashKeys[slot] == key) { hits++; acc += hashVals[slot]; break; }
			slot = (slot + 1) & (HASH_SIZE - 1);
		}
	}
	return hits * 0x9E3779B1u ^ acc;
}

// Linear RGB565 -> PICA tiled (8x8 Morton) layout, as a texture upload helper.
void thumb_swizzle565(u16* dst, const u16* src, int w, int h) {
	static const u8 morton[64] = {
		0,  1,  4,  5,  16, 17, 20, 21, 2,  3,  6,  7,  18, 19, 22, 23,
		8,  9,  12, 13, 24, 25, 28, 29, 10, 11, 14, 15, 26, 27, 30, 31,
		32, 33, 36, 37, 48, 49, 52, 53, 34, 35, 38, 39, 50, 51, 54, 55,
		40, 41, 44, 45, 56, 57, 60, 61, 42, 43, 46, 47, 58, 59, 62, 63,
	};
	for (int ty = 0; ty < h; ty += 8) {
		for (int tx = 0; tx < w; tx += 8) {
			// PICA textures are stored bottom-up
			u16* tile = dst + ((h - 8 - ty) / 8 * (w / 8) + tx / 8) * 64;
			for (int y = 0; y < 8; y++) {
				const u16* row = src + (ty + y) * w + tx;
				for (int x = 0; x < 8; x++) tile[morton[(7 - y) * 8 + x]] = row[x];
			}
		}
	}
}
