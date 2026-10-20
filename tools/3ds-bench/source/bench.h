// Banana 3DS Bench - shared declarations
#pragma once
#include <3ds.h>
#include <citro3d.h>
#include <citro2d.h>
#include <stdbool.h>

#define FRAMES_PER_SCENE 300

enum { SC_CPU, SC_3D, SC_2D, SC_MIXED, SC_FILL, SC_COUNT };

typedef struct {
	u32 tris;     // triangles submitted to the GPU (3D path)
	u32 draws;    // draw calls (3D path, incl. immediate-mode)
	u32 sprites;  // citro2d sprites/images drawn
	u32 uploads;  // bytes of texture data uploaded/transferred by the CPU path
} BenchStats;

extern BenchStats g_stats;
extern C3D_RenderTarget* g_top;
extern C3D_RenderTarget* g_bot;
extern bool g_textOK;

// ---- cpu_work.c (ARM, VFP) ----
void cpu_init(void);
// Game-logic-like CPU work for one frame. Returns a checksum of the results.
u32 cpu_frame(u32 frame, int entities, int sortN, int hashN, int memKB);

// ---- cpu_thumb.c (Thumb-1, integer only) ----
void thumb_init(void);
u32 thumb_sort(u32* keys, u32* vals, u32* tmpk, u32* tmpv, int n);
u32 thumb_hash_bench(u32 seed, int n);
u32 thumb_crc32(const u8* p, u32 n, u32 crc);
void thumb_swizzle565(u16* dst, const u16* src, int w, int h);

// ---- scene3d.c ----
typedef struct {
	int terrainChunks; // 0..16, 288 tris each, ETC1, fog
	int tori;          // 0..8, 576 tris each, fragment lighting (LUTs), RGBA8 mipmapped
	int heroes;        // 0..4, 768-tri spheres, fragment lighting
	int props;         // lit props (sphere 160 / cube 12 tris), RGB565/LA8, every 10th alpha-blended
	int treeGroups;    // 0..6 groups of 10 cross-quad trees, ETC1A4 + alpha test
	int particles;     // dynamic (rewritten per frame) additive A8 billboards, 2 tris each
	int ribbons;       // immediate-mode triangle strips, 98 tris each
	bool rtt;          // render-to-texture pass + TextureCopy + DisplayTransfer downscale
	bool water;        // alpha-blended 3-stage combiner water plane with fog
} World3D;

void r3d_init(void);
void r3d_draw_world(C3D_RenderTarget* target, u32 frame, const World3D* w);
void r3d_draw_fill(C3D_RenderTarget* target, bool top, u32 frame, int layers);
C3D_Tex* r3d_rtt_tex(void);
void r3d_reset_state(void);

// ---- scene2d.c ----
void r2d_init(void);
// Call once per frame before any C2D drawing (after all 3D drawing of the frame).
void r2d_begin(void);
void r2d_update_dynamic_textures(u32 frame);
void r2d_draw_sprites(u32 frame, int first, int count, float screenW, bool fancy);
void r2d_draw_dynamic(u32 frame, bool top);
void r2d_draw_image_tex(C3D_Tex* tex, float x, float y, float w, float h, float angle);
void r2d_draw_hud(u32 frame, int scene, const char* name, int sprites);
void r2d_draw_text(float x, float y, float scale, u32 color, const char* fmt, ...);
void r2d_text_clear(void);
void r2d_draw_overlay(u32 frame, int scene, const char* name, float fps, float frameMs);

// ---- audio.c ----
void audio_init(void);
void audio_play(bool on);
void audio_exit(void);
bool audio_ok(void);
