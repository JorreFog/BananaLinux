// Banana 3DS Bench - deterministic 3DS homebrew stress test for emulator benchmarking.
//
// The scene is chosen purely from the count of rendered frames:
//   scene = (frame / FRAMES_PER_SCENE) % SC_COUNT
// so frame N always renders the same content. Markers are emitted through
// svcOutputDebugString (Panda3DS prints them as "[OutputDebugString] BENCH ...").
#include "bench.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define DISPLAY_TRANSFER_FLAGS                                                                              \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |                        \
	 GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |          \
	 GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

BenchStats g_stats;
C3D_RenderTarget* g_top;
C3D_RenderTarget* g_bot;
bool g_textOK;

static const char* const sceneNames[SC_COUNT] = { "cpu", "3d", "2d", "mixed", "fill" };

// 3D workloads per scene
static const World3D worldCpu = { .terrainChunks = 4, .tori = 1, .props = 20, .treeGroups = 1 };
static const World3D worldHeavy = {
	.terrainChunks = 16, .tori = 8, .heroes = 2, .props = 180, .treeGroups = 6,
	.particles = 600, .ribbons = 4, .rtt = true, .water = true,
};
static const World3D worldMixed = {
	.terrainChunks = 16, .tori = 2, .props = 60, .treeGroups = 6, .particles = 200, .ribbons = 2, .water = true,
};

static void marker(const char* fmt, ...) {
	char buf[192];
	va_list va;
	va_start(va, fmt);
	int n = vsnprintf(buf, sizeof(buf), fmt, va);
	va_end(va);
	if (n < 0) return;
	if (n >= (int)sizeof(buf)) n = sizeof(buf) - 1;
	svcOutputDebugString(buf, n);
}

int main(void) {
	gfxInitDefault();
	C3D_Init(0x80000); // 512 KB command buffer (heavy scenes use ~1/3 of it)
	C2D_Init(6144);

	g_top = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	g_bot = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	C3D_RenderTargetSetOutput(g_top, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
	C3D_RenderTargetSetOutput(g_bot, GFX_BOTTOM, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

	g_textOK = R_SUCCEEDED(fontEnsureMapped());

	cpu_init();
	r3d_init();
	r2d_init();
	audio_init();

	marker("BENCH start version=1 scenes=%d frames_per_scene=%d text=%d audio=%d", SC_COUNT, FRAMES_PER_SCENE,
	       g_textOK, audio_ok());

	const u32 loopLen = FRAMES_PER_SCENE * SC_COUNT;
	u32 frame = 0;
	u32 sceneHash = 0;
	u64 lastTick = svcGetSystemTick();
	float fps = 60.0f, frameMs = 16.7f;

	while (aptMainLoop()) {
		hidScanInput();
		if (hidKeysDown() & KEY_START) break;

		u32 lf = frame % loopLen;
		int sc = (int)(lf / FRAMES_PER_SCENE);
		u32 local = lf % FRAMES_PER_SCENE;
		if (local == 0) {
			if (frame) {
				int prev = (sc + SC_COUNT - 1) % SC_COUNT;
				marker("BENCH scene_end scene=%d name=%s frame=%lu hash=%08lX", prev, sceneNames[prev], frame, sceneHash);
			}
			if (lf == 0 && frame) marker("BENCH loop n=%lu frame=%lu", frame / loopLen, frame);
			marker("BENCH scene=%d name=%s frame=%lu", sc, sceneNames[sc], frame);
			audio_play(sc == SC_MIXED);
			sceneHash = 0;
		}
		memset(&g_stats, 0, sizeof(g_stats));

		// ---- CPU (game logic) ----
		u32 h = 0;
		switch (sc) {
		case SC_CPU:   h = cpu_frame(frame, 2000, 4096, 4096, 384); break;
		case SC_MIXED: h = cpu_frame(frame, 700, 1366, 1366, 128); break;
		default:       h = cpu_frame(frame, 64, 128, 128, 16); break; // token logic in GPU-bound scenes
		}
		sceneHash = (sceneHash ^ h) * 16777619u;

		// ---- GPU ----
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		r2d_text_clear();
		if (sc == SC_2D) r2d_update_dynamic_textures(frame);
		C3D_RenderTargetClear(g_top, C3D_CLEAR_ALL, 0x6080B0FF, 0);
		C3D_RenderTargetClear(g_bot, C3D_CLEAR_ALL, 0x202030FF, 0);

		switch (sc) {
		case SC_CPU:
			r3d_draw_world(g_top, frame, &worldCpu);
			r2d_begin();
			C2D_SceneBegin(g_bot);
			r2d_draw_hud(frame, sc, sceneNames[sc], 48);
			break;
		case SC_3D:
			r3d_draw_world(g_top, frame, &worldHeavy);
			r2d_begin();
			C2D_SceneBegin(g_bot);
			r2d_draw_image_tex(r3d_rtt_tex(), 96, 30, 128, 128, 0.0f); // minimap from the RTT pass
			r2d_draw_hud(frame, sc, sceneNames[sc], 120);
			break;
		case SC_2D:
			r2d_begin();
			C2D_SceneBegin(g_top);
			r2d_draw_dynamic(frame, true);
			r2d_draw_sprites(frame, 0, 1200, 400.0f, true);
			r2d_draw_text(8, 8, 0.8f, C2D_Color32(255, 255, 255, 255), "Sprites: 1200 top + 800 bottom");
			r2d_draw_text(8, 200, 0.6f, C2D_Color32(255, 220, 80, 255), "frame %lu  rot/scale/tint/alpha", frame);
			C2D_SceneBegin(g_bot);
			r2d_draw_dynamic(frame, false);
			r2d_draw_sprites(frame, 1200, 800, 320.0f, true);
			r2d_draw_text(8, 8, 0.6f, C2D_Color32(255, 255, 255, 255), "Dynamic textures: GX_DisplayTransfer + TexUpload");
			break;
		case SC_MIXED:
			r3d_draw_world(g_top, frame, &worldMixed);
			r2d_begin();
			C2D_SceneBegin(g_top);
			r2d_draw_text(8, 6, 0.7f, C2D_Color32(255, 255, 255, 255), "TIME %02lu:%02lu", frame / 3600 % 60, frame / 60 % 60);
			C2D_SceneBegin(g_bot);
			r2d_draw_hud(frame, sc, sceneNames[sc], 280);
			break;
		case SC_FILL:
			r3d_draw_fill(g_top, true, frame, 6);
			r3d_draw_fill(g_bot, false, frame, 6);
			r2d_begin();
			C2D_SceneBegin(g_bot);
			break;
		}
		// overlay (bottom screen, C2D scene already active on g_bot except in SC_2D)
		C2D_SceneBegin(g_bot);
		r2d_draw_overlay(frame, sc, sceneNames[sc], fps, frameMs);
		C3D_FrameEnd(0);

		if (local == 0)
			marker("BENCH stats scene=%d name=%s tris=%lu draws=%lu sprites=%lu xfer_bytes=%lu cmdbuf=%.3f", sc,
			       sceneNames[sc], g_stats.tris, g_stats.draws, g_stats.sprites, g_stats.uploads, C3D_GetCmdBufUsage());

		u64 now = svcGetSystemTick();
		float ms = (float)(now - lastTick) * (1000.0f / SYSCLOCK_ARM11);
		lastTick = now;
		frameMs = frameMs * 0.9f + ms * 0.1f;
		fps = frameMs > 0.0f ? 1000.0f / frameMs : 0.0f;
		frame++;
	}

	marker("BENCH exit frame=%lu", frame);
	audio_exit();
	C2D_Fini();
	C3D_Fini();
	gfxExit();
	return 0;
}
