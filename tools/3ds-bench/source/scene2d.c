// 2D renderer (citro2d): sprite batches from an atlas, per-frame texture
// uploads, system-font text, HUD and the FPS overlay.
#include "bench.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "sprites_t3x.h"

#define MAX_SPRITES 3200

typedef struct {
	float x, y, vx, vy, rot, vrot, scale;
	u16 img;
	u8 tint, pad;
} Spr;

static C2D_SpriteSheet sheet;
static int sheetCount;
static Spr spr[MAX_SPRITES];
static u32 sprFrame = 0xFFFFFFFF;

// dynamic textures
static C3D_Tex dynPlasma;            // 128x128 RGBA8, CPU -> linear buffer -> GX_DisplayTransfer (tiling)
static u32* plasmaLin[2];
static C3D_Tex dynSwz;               // 128x128 RGB565, CPU-swizzled -> C3D_TexUpload
static u16* swzLin;
static u16* swzTiled;
static Tex3DS_SubTexture subPlasma = { 128, 128, 0.0f, 1.0f, 1.0f, 0.0f };
static Tex3DS_SubTexture subSwz = { 128, 128, 0.0f, 1.0f, 1.0f, 0.0f };
static Tex3DS_SubTexture subRtt = { 256, 256, 0.0f, 1.0f, 1.0f, 0.0f };
static s8 sinLut[256];

static C2D_TextBuf textBuf;

void r2d_init(void) {
	sheet = C2D_SpriteSheetLoadFromMem(sprites_t3x, sprites_t3x_size);
	if (!sheet) svcBreak(USERBREAK_PANIC);
	sheetCount = (int)C2D_SpriteSheetCount(sheet);

	u32 r = 0xC0FFEE11u;
	for (int i = 0; i < MAX_SPRITES; i++) {
		Spr* s = &spr[i];
#define RND() (r ^= r << 13, r ^= r >> 17, r ^= r << 5, r)
		s->x = (float)(RND() % 400);
		s->y = (float)(RND() % 240);
		s->vx = ((int)(RND() % 200) - 100) * 0.02f;
		s->vy = ((int)(RND() % 200) - 100) * 0.02f;
		s->rot = (RND() % 628) * 0.01f;
		s->vrot = ((int)(RND() % 100) - 50) * 0.002f;
		s->scale = 0.35f + (RND() % 100) * 0.008f;
		s->img = (u16)(RND() % sheetCount);
		s->tint = (u8)(RND() % 4);
#undef RND
	}

	for (int i = 0; i < 256; i++) sinLut[i] = (s8)(127.0f * sinf(i * (2.0f * M_PI / 256.0f)));

	C3D_TexInit(&dynPlasma, 128, 128, GPU_RGBA8);
	C3D_TexSetFilter(&dynPlasma, GPU_LINEAR, GPU_LINEAR);
	for (int i = 0; i < 2; i++) plasmaLin[i] = (u32*)linearAlloc(128 * 128 * 4);
	C3D_TexInitVRAM(&dynSwz, 128, 128, GPU_RGB565);
	C3D_TexSetFilter(&dynSwz, GPU_LINEAR, GPU_LINEAR);
	swzLin = (u16*)linearAlloc(128 * 128 * 2);
	swzTiled = (u16*)linearAlloc(128 * 128 * 2);

	textBuf = C2D_TextBufNew(4096);
}

void r2d_begin(void) {
	r3d_reset_state();
	C2D_Prepare();
}

// Sprite simulation is shared by all scenes; run once per frame.
static void updateSprites(u32 frame) {
	if (sprFrame == frame) return;
	sprFrame = frame;
	for (int i = 0; i < MAX_SPRITES; i++) {
		Spr* s = &spr[i];
		s->x += s->vx;
		s->y += s->vy;
		if (s->x < -16) s->x += 432; else if (s->x > 416) s->x -= 432;
		if (s->y < -16) s->y += 272; else if (s->y > 256) s->y -= 272;
		s->rot += s->vrot;
	}
}

void r2d_draw_sprites(u32 frame, int first, int count, float screenW, bool fancy) {
	updateSprites(frame);
	C2D_ImageTint tints[4];
	C2D_PlainImageTint(&tints[1], C2D_Color32(255, 80, 80, 255), 0.5f);
	C2D_AlphaImageTint(&tints[2], 0.5f);
	C2D_PlainImageTint(&tints[3], C2D_Color32(80, 160, 255, 200), 0.35f);
	for (int i = first; i < first + count && i < MAX_SPRITES; i++) {
		const Spr* s = &spr[i];
		C2D_Image img = C2D_SpriteSheetGetImage(sheet, s->img);
		float sc = fancy ? s->scale * (1.0f + 0.25f * sinLut[(frame * 2 + i) & 255] / 127.0f) : s->scale;
		float w = img.subtex->width * sc, h = img.subtex->height * sc;
		C2D_DrawParams p = {
			{ s->x * (screenW / 400.0f), s->y, w, h },
			{ w * 0.5f, h * 0.5f },
			0.5f,
			fancy ? s->rot : 0.0f,
		};
		C2D_DrawImage(img, &p, s->tint ? &tints[s->tint] : NULL);
	}
	g_stats.sprites += count;
}

// Per-frame texture updates (call after C3D_FrameBegin, before drawing).
void r2d_update_dynamic_textures(u32 frame) {
	// 1) 128x128 RGBA8 plasma, linear -> tiled via GX_DisplayTransfer
	u32* lin = plasmaLin[frame & 1];
	u32 t = frame;
	for (int y = 0; y < 128; y++) {
		int sy = sinLut[(y * 2 + t) & 255];
		for (int x = 0; x < 128; x++) {
			int v = sinLut[(x * 3 + t * 2) & 255] + sy + sinLut[((x + y) * 2 - t) & 255];
			u8 r = (u8)(128 + sinLut[(v + t) & 255]), g = (u8)(128 + sinLut[(v * 2 + 64) & 255]), b = (u8)(128 + sinLut[(v + 128 - t) & 255]);
			lin[y * 128 + x] = ((u32)r << 24) | ((u32)g << 16) | ((u32)b << 8) | 0xFF;
		}
	}
	GSPGPU_FlushDataCache(lin, 128 * 128 * 4);
	GX_DisplayTransfer(lin, GX_BUFFER_DIM(128, 128), (u32*)dynPlasma.data, GX_BUFFER_DIM(128, 128),
	                   GX_TRANSFER_FLIP_VERT(1) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |
	                   GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
	                   GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));

	// 2) 128x128 RGB565 "software renderer" output, swizzled in Thumb code, C3D_TexUpload
	for (int y = 0; y < 128; y++) {
		u16* row = swzLin + y * 128;
		int cy = (y - 64) * (y - 64);
		for (int x = 0; x < 128; x++) {
			int d = ((x - 64) * (x - 64) + cy) >> 4;
			int v = (d - (int)t * 3) & 63;
			row[x] = (u16)(((v >> 1) << 11) | (((x ^ y ^ t) & 63) << 5) | (31 - (v >> 1)));
		}
	}
	thumb_swizzle565(swzTiled, swzLin, 128, 128);
	C3D_TexUpload(&dynSwz, swzTiled);
	g_stats.uploads += 128 * 128 * 4 + 128 * 128 * 2;
}

void r2d_draw_image_tex(C3D_Tex* tex, float x, float y, float w, float h, float angle) {
	Tex3DS_SubTexture* sub = tex == &dynPlasma ? &subPlasma : tex == &dynSwz ? &subSwz : &subRtt;
	C2D_Image img = { tex, sub };
	C2D_DrawParams p = { { x + w * 0.5f, y + h * 0.5f, w, h }, { w * 0.5f, h * 0.5f }, 0.5f, angle };
	C2D_DrawImage(img, &p, NULL);
	g_stats.sprites++;
}

void r2d_draw_dynamic(u32 frame, bool top) {
	float sw = top ? 400.0f : 320.0f;
	r2d_draw_image_tex(&dynSwz, 0, 0, sw, 240, 0.0f);
	r2d_draw_image_tex(&dynPlasma, sw * 0.5f - 80, 40, 160, 160, frame * 0.01f);
}

void r2d_draw_text(float x, float y, float scale, u32 color, const char* fmt, ...) {
	if (!g_textOK) return;
	char buf[160];
	va_list va;
	va_start(va, fmt);
	vsnprintf(buf, sizeof(buf), fmt, va);
	va_end(va);
	C2D_Text txt;
	C2D_TextParse(&txt, textBuf, buf);
	C2D_TextOptimize(&txt);
	C2D_DrawText(&txt, C2D_WithColor, x, y, 0.6f, scale, scale, color);
}

// Typical game HUD: panels, icons, health bars, score text.
void r2d_draw_hud(u32 frame, int scene, const char* name, int sprites) {
	(void)scene;
	// panels and bars
	C2D_DrawRectSolid(0, 0, 0.4f, 320, 28, C2D_Color32(20, 20, 40, 200));
	C2D_DrawRectangle(0, 200, 0.4f, 320, 40, C2D_Color32(40, 0, 0, 220), C2D_Color32(40, 0, 0, 220),
	                  C2D_Color32(10, 10, 60, 220), C2D_Color32(10, 10, 60, 220));
	for (int i = 0; i < 8; i++) {
		float hp = 0.5f + 0.5f * sinLut[(frame + i * 30) & 255] / 127.0f;
		C2D_DrawRectSolid(8 + i * 39, 206, 0.45f, 34, 6, C2D_Color32(60, 60, 60, 255));
		C2D_DrawRectSolid(8 + i * 39, 206, 0.46f, 34 * hp, 6, C2D_Color32(40, 220, 60, 255));
	}
	g_stats.sprites += 18;
	// icon grid (inventory)
	r2d_draw_sprites(frame, 0, sprites, 320.0f, false);
	r2d_draw_text(6, 4, 0.6f, C2D_Color32(255, 255, 255, 255), "SCORE %07lu  %s", frame * 13 % 9999999, name);
	r2d_draw_text(6, 218, 0.5f, C2D_Color32(255, 230, 120, 255), "LV %lu  HP %lu/%lu  MP %lu", 1 + frame / 600,
	              100 + (frame % 50), 150, frame % 99);
}

void r2d_draw_overlay(u32 frame, int scene, const char* name, float fps, float frameMs) {
	C2D_DrawRectSolid(0, 160, 0.55f, 320, 40, C2D_Color32(0, 0, 0, 160));
	r2d_draw_text(4, 162, 0.5f, C2D_Color32(255, 255, 0, 255), "BENCH %d:%s  frame %lu", scene, name, frame);
#ifdef BENCH_FPS_OVERLAY
	// Timing-dependent, so off by default: frame dumps must depend only on the frame number.
	float w = frameMs / 33.3f * 320.0f; // 16.7 ms = half width
	if (w > 320) w = 320;
	C2D_DrawRectSolid(0, 196, 0.56f, w, 4, frameMs > 17.5f ? C2D_Color32(255, 60, 60, 255) : C2D_Color32(60, 255, 60, 255));
	r2d_draw_text(4, 178, 0.5f, C2D_Color32(255, 255, 255, 255), "%.1f fps  %.2f ms  cpu %.1f gpu %.1f", fps, frameMs,
	              C3D_GetProcessingTime(), C3D_GetDrawingTime());
#else
	(void)fps;
	(void)frameMs;
#endif
}

void r2d_text_clear(void) { C2D_TextBufClear(textBuf); }
