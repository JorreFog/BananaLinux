// 3D renderer for the "3d", "mixed", "cpu" and "fill" scenes (citro3d).
#include "bench.h"
#include <math.h>
#include <string.h>

#include "lit_shbin.h"
#include "fraglit_shbin.h"
#include "unlit_shbin.h"
#include "terrain_t3x.h"
#include "leaf_t3x.h"
#include "metal_t3x.h"
#include "brick_t3x.h"
#include "checker_t3x.h"
#include "glow_t3x.h"
#include "noise_t3x.h"
#include "water_t3x.h"

typedef struct { float pos[3]; float tc[2]; float nrm[3]; } VtxL; // lit / fragment-lit
typedef struct { float pos[3]; float tc[2]; u8 clr[4]; } VtxU;     // unlit, u8 colour
typedef struct { u32 first, count; } Mesh;                          // range in the index buffer

#define MAX_VL   8192
#define MAX_IL   40000
#define MAX_VU   64
#define MAX_PART 1024

static DVLB_s *dvLit, *dvFrag, *dvUnlit;
static shaderProgram_s pLit, pFrag, pUnlit;
static struct { int proj, mv, lightVec, lightClr, ambient; } uLit;
static struct { int proj, mv; } uFrag;
static struct { int proj, mv, tcx1, tcx2; } uUnlit;

static VtxL* vboL;
static u16* iboL;
static int nVL, nIL;
static VtxU* vboU;
static int nVU;
static VtxU* dynU[2];

static Mesh mTerrain[16], mTorus, mSphereHi, mSphereLo, mCube, mTrees[6];
static int qWater, qMonitor, qFullTop, qFullBot;

static C3D_Tex texTerrain, texLeaf, texMetal, texBrick, texChecker, texGlow, texNoise, texWater;
static C3D_Tex rttTex, histTex, bloomTex;
static u32* bloomLin;
static C3D_RenderTarget* rttTarget;

static C3D_FogLut fogLut;
static C3D_LightEnv lightEnv;
static C3D_Light light;
static C3D_LightLut lutD0, lutD1;

static C3D_Mtx proj, view;

// ---------------------------------------------------------------------------
// mesh building

static float terrainH(float x, float z) {
	return 2.5f * sinf(x * 0.15f) * cosf(z * 0.13f) + 1.2f * sinf((x + z) * 0.31f) - 1.0f;
}

static u16 addV(float x, float y, float z, float u, float v, float nx, float ny, float nz) {
	VtxL* p = &vboL[nVL];
	p->pos[0] = x; p->pos[1] = y; p->pos[2] = z;
	p->tc[0] = u;  p->tc[1] = v;
	p->nrm[0] = nx; p->nrm[1] = ny; p->nrm[2] = nz;
	return (u16)nVL++;
}

// Adds a triangle wound CCW as seen from the side its vertex normals point to.
static void addTri(u16 a, u16 b, u16 c) {
	const float *pa = vboL[a].pos, *pb = vboL[b].pos, *pc = vboL[c].pos;
	float e1[3] = { pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2] };
	float e2[3] = { pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2] };
	float cr[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
	float n[3];
	for (int k = 0; k < 3; k++) n[k] = vboL[a].nrm[k] + vboL[b].nrm[k] + vboL[c].nrm[k];
	if (cr[0] * n[0] + cr[1] * n[1] + cr[2] * n[2] < 0.0f) { u16 t = b; b = c; c = t; }
	iboL[nIL++] = a; iboL[nIL++] = b; iboL[nIL++] = c;
}

static void addQuadRaw(u16 a, u16 b, u16 c, u16 d) {
	iboL[nIL++] = a; iboL[nIL++] = b; iboL[nIL++] = c;
	iboL[nIL++] = a; iboL[nIL++] = c; iboL[nIL++] = d;
}

// Grid of (nu+1)*(nv+1) vertices produced by fn, two triangles per cell.
typedef void (*SurfFn)(float u, float v, float* pos, float* nrm, void* ud);
static Mesh buildSurface(int nu, int nv, SurfFn fn, void* ud, float tu, float tv) {
	Mesh m = { (u32)nIL, 0 };
	u16 base = (u16)nVL;
	for (int j = 0; j <= nv; j++)
		for (int i = 0; i <= nu; i++) {
			float p[3], n[3], u = (float)i / nu, v = (float)j / nv;
			fn(u, v, p, n, ud);
			addV(p[0], p[1], p[2], u * tu, v * tv, n[0], n[1], n[2]);
		}
	for (int j = 0; j < nv; j++)
		for (int i = 0; i < nu; i++) {
			u16 a = base + j * (nu + 1) + i, b = a + 1, c = a + (nu + 1), d = c + 1;
			addTri(a, c, b);
			addTri(b, c, d);
		}
	m.count = nIL - m.first;
	return m;
}

static void surfSphere(float u, float v, float* p, float* n, void* ud) {
	(void)ud;
	float th = v * M_PI, ph = u * 2.0f * M_PI;
	n[0] = sinf(th) * cosf(ph); n[1] = cosf(th); n[2] = sinf(th) * sinf(ph);
	if (n[0] * n[0] + n[1] * n[1] + n[2] * n[2] < 1e-6f) n[1] = 1.0f;
	for (int k = 0; k < 3; k++) p[k] = n[k];
}

static void surfTorus(float u, float v, float* p, float* n, void* ud) {
	(void)ud;
	const float R = 1.2f, r = 0.45f;
	float a = u * 2.0f * M_PI, b = v * 2.0f * M_PI;
	n[0] = cosf(a) * cosf(b); n[1] = sinf(b); n[2] = sinf(a) * cosf(b);
	p[0] = cosf(a) * R + n[0] * r; p[1] = n[1] * r; p[2] = sinf(a) * R + n[2] * r;
}

typedef struct { float x0, z0, size; } Chunk;
static void surfTerrain(float u, float v, float* p, float* n, void* ud) {
	Chunk* c = (Chunk*)ud;
	float x = c->x0 + u * c->size, z = c->z0 + v * c->size;
	p[0] = x; p[1] = terrainH(x, z); p[2] = z;
	float e = 0.5f;
	float dx = (terrainH(x + e, z) - terrainH(x - e, z)) / (2 * e);
	float dz = (terrainH(x, z + e) - terrainH(x, z - e)) / (2 * e);
	float l = 1.0f / sqrtf(dx * dx + 1 + dz * dz);
	n[0] = -dx * l; n[1] = l; n[2] = -dz * l;
}

static Mesh buildCube(void) {
	Mesh m = { (u32)nIL, 0 };
	static const float N[6][3] = { { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
	for (int f = 0; f < 6; f++) {
		const float* n = N[f];
		// two tangent axes
		float t1[3] = { n[1] != 0 ? 1.0f : 0.0f, n[1] != 0 ? 0.0f : 1.0f, 0 };
		float t2[3] = { n[1] * t1[2] - n[2] * t1[1], n[2] * t1[0] - n[0] * t1[2], n[0] * t1[1] - n[1] * t1[0] };
		u16 v[4];
		for (int k = 0; k < 4; k++) {
			float s = (k == 1 || k == 2) ? 0.5f : -0.5f, t = (k >= 2) ? 0.5f : -0.5f;
			v[k] = addV(n[0] * 0.5f + t1[0] * s + t2[0] * t, n[1] * 0.5f + t1[1] * s + t2[1] * t,
			            n[2] * 0.5f + t1[2] * s + t2[2] * t, s + 0.5f, t + 0.5f, n[0], n[1], n[2]);
		}
		addTri(v[0], v[1], v[2]);
		addTri(v[0], v[2], v[3]);
	}
	m.count = nIL - m.first;
	return m;
}

static u32 hash32(u32 x) {
	x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
	return x;
}
static float hashf(u32 x) { return (float)(hash32(x) & 0xFFFF) / 65535.0f; }

static Mesh buildTrees(int group) {
	Mesh m = { (u32)nIL, 0 };
	for (int t = 0; t < 10; t++) {
		u32 id = group * 10 + t;
		float x = (hashf(id * 3 + 1) * 2 - 1) * 44, z = (hashf(id * 3 + 2) * 2 - 1) * 44;
		float y = terrainH(x, z), w = 1.6f, h = 4.5f;
		for (int q = 0; q < 2; q++) {
			float dx = q ? 0 : w, dz = q ? w : 0;
			u16 a = addV(x - dx, y, z - dz, 0, 0, 0, 1, 0);
			u16 b = addV(x + dx, y, z + dz, 1, 0, 0, 1, 0);
			u16 c = addV(x + dx, y + h, z + dz, 1, 1, 0, 1, 0);
			u16 d = addV(x - dx, y + h, z - dz, 0, 1, 0, 1, 0);
			addQuadRaw(a, b, c, d);
		}
	}
	m.count = nIL - m.first;
	return m;
}

static int addQuadU(float x0, float y0, float x1, float y1, float z, float u0, float v0, float u1, float v1, u32 rgba, int plane) {
	int first = nVU;
	float P[4][3], T[4][2] = { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } };
	float X[4] = { x0, x1, x1, x0 }, Y[4] = { y0, y0, y1, y1 };
	for (int k = 0; k < 4; k++) {
		if (plane) { P[k][0] = X[k]; P[k][1] = z; P[k][2] = Y[k]; } // XZ plane at height z
		else       { P[k][0] = X[k]; P[k][1] = Y[k]; P[k][2] = z; } // XY plane
	}
	static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
	for (int k = 0; k < 6; k++) {
		VtxU* v = &vboU[nVU++];
		memcpy(v->pos, P[idx[k]], sizeof(v->pos));
		memcpy(v->tc, T[idx[k]], sizeof(v->tc));
		v->clr[0] = rgba >> 24; v->clr[1] = rgba >> 16; v->clr[2] = rgba >> 8; v->clr[3] = rgba;
	}
	return first;
}

// ---------------------------------------------------------------------------
// init

static void loadTex(C3D_Tex* t, const void* d, size_t sz, bool vram) {
	Tex3DS_Texture tx = Tex3DS_TextureImport(d, sz, t, NULL, vram);
	if (!tx) svcBreak(USERBREAK_PANIC);
	Tex3DS_TextureFree(tx);
	C3D_TexSetFilter(t, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(t, GPU_REPEAT, GPU_REPEAT);
}

static void loadProg(DVLB_s** dv, shaderProgram_s* p, const void* bin, u32 size) {
	*dv = DVLB_ParseFile((u32*)bin, size);
	shaderProgramInit(p);
	shaderProgramSetVsh(p, &(*dv)->DVLE[0]);
}

void r3d_init(void) {
	loadProg(&dvLit, &pLit, lit_shbin, lit_shbin_size);
	loadProg(&dvFrag, &pFrag, fraglit_shbin, fraglit_shbin_size);
	loadProg(&dvUnlit, &pUnlit, unlit_shbin, unlit_shbin_size);
	uLit.proj = shaderInstanceGetUniformLocation(pLit.vertexShader, "projection");
	uLit.mv = shaderInstanceGetUniformLocation(pLit.vertexShader, "modelView");
	uLit.lightVec = shaderInstanceGetUniformLocation(pLit.vertexShader, "lightVec");
	uLit.lightClr = shaderInstanceGetUniformLocation(pLit.vertexShader, "lightClr");
	uLit.ambient = shaderInstanceGetUniformLocation(pLit.vertexShader, "ambient");
	uFrag.proj = shaderInstanceGetUniformLocation(pFrag.vertexShader, "projection");
	uFrag.mv = shaderInstanceGetUniformLocation(pFrag.vertexShader, "modelView");
	uUnlit.proj = shaderInstanceGetUniformLocation(pUnlit.vertexShader, "projection");
	uUnlit.mv = shaderInstanceGetUniformLocation(pUnlit.vertexShader, "modelView");
	uUnlit.tcx1 = shaderInstanceGetUniformLocation(pUnlit.vertexShader, "tcXform1");
	uUnlit.tcx2 = shaderInstanceGetUniformLocation(pUnlit.vertexShader, "tcXform2");

	// static geometry (one shared VBO + IBO, like a level pack)
	vboL = (VtxL*)linearAlloc(sizeof(VtxL) * MAX_VL);
	iboL = (u16*)linearAlloc(sizeof(u16) * MAX_IL);
	for (int cz = 0; cz < 4; cz++)
		for (int cx = 0; cx < 4; cx++) {
			Chunk c = { -48.0f + cx * 24.0f, -48.0f + cz * 24.0f, 24.0f };
			mTerrain[cz * 4 + cx] = buildSurface(12, 12, surfTerrain, &c, 3.0f, 3.0f);
		}
	mTorus = buildSurface(24, 12, surfTorus, NULL, 4.0f, 2.0f);
	mSphereHi = buildSurface(24, 16, surfSphere, NULL, 2.0f, 1.0f);
	mSphereLo = buildSurface(10, 8, surfSphere, NULL, 2.0f, 1.0f);
	mCube = buildCube();
	for (int g = 0; g < 6; g++) mTrees[g] = buildTrees(g);
	GSPGPU_FlushDataCache(vboL, sizeof(VtxL) * nVL);
	GSPGPU_FlushDataCache(iboL, sizeof(u16) * nIL);

	vboU = (VtxU*)linearAlloc(sizeof(VtxU) * MAX_VU);
	qWater = addQuadU(-48, -48, 48, 48, -1.6f, 0, 0, 12, 12, 0xFFFFFFC0, 1);
	qMonitor = addQuadU(-6, 0, 6, 8, 0, 0, 0, 1, 1, 0xFFFFFFFF, 0);
	qFullTop = addQuadU(0, 0, 400, 240, 0.5f, 0, 0, 400.0f / 128, 240.0f / 128, 0xFFFFFFFF, 0);
	qFullBot = addQuadU(0, 0, 320, 240, 0.5f, 0, 0, 320.0f / 128, 240.0f / 128, 0xFFFFFFFF, 0);
	GSPGPU_FlushDataCache(vboU, sizeof(VtxU) * nVU);
	for (int i = 0; i < 2; i++) dynU[i] = (VtxU*)linearAlloc(sizeof(VtxU) * 6 * MAX_PART);

	// textures in all common formats, some in VRAM, some in linear FCRAM
	loadTex(&texTerrain, terrain_t3x, terrain_t3x_size, true);   // ETC1, mipmapped
	C3D_TexSetFilterMipmap(&texTerrain, GPU_LINEAR);
	loadTex(&texLeaf, leaf_t3x, leaf_t3x_size, false);           // ETC1A4
	loadTex(&texMetal, metal_t3x, metal_t3x_size, true);         // RGBA8, mipmapped
	C3D_TexSetFilterMipmap(&texMetal, GPU_LINEAR);
	loadTex(&texBrick, brick_t3x, brick_t3x_size, false);        // RGB565
	loadTex(&texChecker, checker_t3x, checker_t3x_size, false);  // LA8
	loadTex(&texGlow, glow_t3x, glow_t3x_size, false);           // A8
	loadTex(&texNoise, noise_t3x, noise_t3x_size, true);         // RGBA4
	loadTex(&texWater, water_t3x, water_t3x_size, false);        // RGBA5551

	// render-to-texture target + copies
	C3D_TexInitVRAM(&rttTex, 256, 256, GPU_RGBA8);
	C3D_TexSetFilter(&rttTex, GPU_LINEAR, GPU_LINEAR);
	rttTarget = C3D_RenderTargetCreateFromTex(&rttTex, GPU_TEXFACE_2D, 0, GPU_RB_DEPTH16);
	C3D_TexInitVRAM(&histTex, 256, 256, GPU_RGBA8);
	C3D_TexSetFilter(&histTex, GPU_LINEAR, GPU_LINEAR);
	C3D_TexInit(&bloomTex, 128, 128, GPU_RGBA8);
	C3D_TexSetFilter(&bloomTex, GPU_LINEAR, GPU_LINEAR);
	bloomLin = (u32*)linearAlloc(128 * 128 * 4);

	FogLut_Exp(&fogLut, 0.025f, 1.4f, 0.5f, 140.0f);

	static const C3D_Material mat = {
		{ 0.25f, 0.25f, 0.3f }, // ambient
		{ 0.7f, 0.7f, 0.65f },  // diffuse
		{ 0.8f, 0.8f, 0.8f },   // specular0
		{ 0.3f, 0.5f, 0.9f },   // specular1
		{ 0.0f, 0.0f, 0.0f },   // emission
	};
	C3D_LightEnvInit(&lightEnv);
	C3D_LightEnvMaterial(&lightEnv, &mat);
	LightLut_Phong(&lutD0, 20.0f);
	LightLut_Phong(&lutD1, 4.0f);
	C3D_LightEnvLut(&lightEnv, GPU_LUT_D0, GPU_LUTINPUT_LN, false, &lutD0);
	C3D_LightEnvLut(&lightEnv, GPU_LUT_D1, GPU_LUTINPUT_NH, false, &lutD1);
	C3D_LightInit(&light, &lightEnv);
	C3D_LightColor(&light, 1.0f, 0.95f, 0.85f);
}

C3D_Tex* r3d_rtt_tex(void) { return &rttTex; }

// ---------------------------------------------------------------------------
// state helpers

static void te_reset(int from) {
	for (int i = from; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
}

void r3d_reset_state(void) {
	C3D_LightEnvBind(NULL);
	C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
	C3D_CullFace(GPU_CULL_BACK_CCW);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
	te_reset(0);
}

static void bindLit(const C3D_Mtx* p, float alpha) {
	C3D_BindProgram(&pLit);
	C3D_AttrInfo* ai = C3D_GetAttrInfo();
	AttrInfo_Init(ai);
	AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3);
	AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 2);
	AttrInfo_AddLoader(ai, 2, GPU_FLOAT, 3);
	C3D_BufInfo* bi = C3D_GetBufInfo();
	BufInfo_Init(bi);
	BufInfo_Add(bi, vboL, sizeof(VtxL), 3, 0x210);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLit.proj, p);
	// light direction in view space: a fixed world direction rotated by the view matrix
	C3D_FVec ld = Mtx_MultiplyFVec3(&view, FVec3_Normalize(FVec3_New(0.4f, 0.8f, 0.3f)));
	C3D_FVUnifSet(GPU_VERTEX_SHADER, uLit.lightVec, ld.x, ld.y, ld.z, 0.0f);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, uLit.lightClr, 0.85f, 0.8f, 0.7f, 1.0f);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, uLit.ambient, 0.3f, 0.32f, 0.4f, alpha);

	// stage 0: texture * vertex lighting; stage 1: * per-object tint (constant)
	C3D_TexEnv* e = C3D_GetTexEnv(0);
	C3D_TexEnvInit(e);
	C3D_TexEnvSrc(e, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
	C3D_TexEnvFunc(e, C3D_Both, GPU_MODULATE);
	e = C3D_GetTexEnv(1);
	C3D_TexEnvInit(e);
	C3D_TexEnvSrc(e, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT, 0);
	C3D_TexEnvFunc(e, C3D_RGB, GPU_MODULATE);
	C3D_TexEnvColor(e, 0xFFFFFFFF);
	te_reset(2);
}

static void bindUnlit(const C3D_Mtx* p, void* vbo) {
	C3D_BindProgram(&pUnlit);
	C3D_AttrInfo* ai = C3D_GetAttrInfo();
	AttrInfo_Init(ai);
	AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3);
	AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 2);
	AttrInfo_AddLoader(ai, 2, GPU_UNSIGNED_BYTE, 4);
	C3D_BufInfo* bi = C3D_GetBufInfo();
	BufInfo_Init(bi);
	BufInfo_Add(bi, vbo, sizeof(VtxU), 3, 0x210);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uUnlit.proj, p);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, uUnlit.tcx1, 1.0f, 1.0f, 0.0f, 0.0f);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, uUnlit.tcx2, 1.0f, 1.0f, 0.0f, 0.0f);
}

static inline void drawMesh(const Mesh* m) {
	C3D_DrawElements(GPU_TRIANGLES, m->count, C3D_UNSIGNED_SHORT, iboL + m->first);
	g_stats.draws++;
	g_stats.tris += m->count / 3;
}

static inline void drawArr(int first, int count) {
	C3D_DrawArrays(GPU_TRIANGLES, first, count);
	g_stats.draws++;
	g_stats.tris += count / 3;
}

static void setMV(int loc, const C3D_Mtx* model) {
	C3D_Mtx mv;
	Mtx_Multiply(&mv, &view, model);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, loc, &mv);
}

static void propModel(C3D_Mtx* m, int i, float t) {
	float x = (hashf(i * 7 + 11) * 2 - 1) * 42, z = (hashf(i * 7 + 12) * 2 - 1) * 42;
	float s = 0.6f + hashf(i * 7 + 13) * 0.9f;
	float y = terrainH(x, z) + s + 0.4f * sinf(t * 2.0f + i);
	Mtx_Identity(m);
	Mtx_Translate(m, x, y, z, true);
	Mtx_RotateY(m, t * (0.5f + hashf(i) * 2.0f) + i, true);
	Mtx_RotateX(m, t * 0.7f + i * 0.3f, true);
	Mtx_Scale(m, s, s, s);
}

static void drawProps(int first, int count, float t, bool translucent) {
	C3D_TexEnv* tint = C3D_GetTexEnv(1);
	for (int i = first; i < first + count; i++) {
		bool tr = (i % 10) == 9;
		if (tr != translucent) continue;
		C3D_Mtx m;
		propModel(&m, i, t);
		setMV(uLit.mv, &m);
		C3D_TexBind(0, (i & 1) ? &texChecker : &texBrick);
		u32 h = hash32(i * 31 + 5);
		C3D_TexEnvColor(tint, 0xFF000000 | (h & 0x7F7F7F) | 0x808080);
		drawMesh((i % 4) == 0 ? &mSphereLo : &mCube);
	}
}

// ---------------------------------------------------------------------------
// render-to-texture pass (overhead "security camera" view) + GX copies

static void rttPass(u32 frame, float t) {
	// keep last frame's RTT image as a history texture (raw TextureCopy), before re-rendering
	static bool rttValid;
	if (rttValid)
		GX_TextureCopy((u32*)rttTex.data, GX_BUFFER_DIM(256 * 8 * 4 / 16, 0), (u32*)histTex.data,
		               GX_BUFFER_DIM(256 * 8 * 4 / 16, 0), 256 * 256 * 4, 0);
	rttValid = true;
	g_stats.uploads += 256 * 256 * 4;

	C3D_RenderTargetClear(rttTarget, C3D_CLEAR_ALL, 0x302018FF, 0);
	C3D_FrameDrawOn(rttTarget);
	C3D_Mtx p, saveView = view;
	Mtx_Persp(&p, C3D_AngleFromDegrees(60.0f), 1.0f, 0.5f, 100.0f, false);
	float a = frame * 0.01f;
	C3D_FVec eye = FVec3_New(sinf(a) * 10.0f, 30.0f, cosf(a) * 10.0f);
	Mtx_LookAt(&view, eye, FVec3_New(0, 0, 0), FVec3_New(0, 1, 0), false);
	bindLit(&p, 1.0f);
	C3D_TexBind(0, &texMetal);
	C3D_Mtx m;
	Mtx_Identity(&m);
	Mtx_Translate(&m, 0, 3, 0, true);
	Mtx_RotateY(&m, t, true);
	Mtx_Scale(&m, 4, 4, 4);
	setMV(uLit.mv, &m);
	drawMesh(&mTorus);
	drawProps(0, 24, t, false);
	view = saveView;

	// flush the RTT commands so the transfers below see the rendered image
	C3D_FrameSplit(0);
	// 2x downscale (tiled -> linear, box filter) then re-tile into a texture: a "bloom" chain.
	// With scaling, the output dimensions are given unscaled (the hardware halves them).
	GX_DisplayTransfer((u32*)rttTex.data, GX_BUFFER_DIM(256, 256), bloomLin, GX_BUFFER_DIM(256, 256),
	                   GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
	                   GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
	                   GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_XY));
	GX_DisplayTransfer(bloomLin, GX_BUFFER_DIM(128, 128), (u32*)bloomTex.data, GX_BUFFER_DIM(128, 128),
	                   GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |
	                   GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
	                   GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
	g_stats.uploads += 256 * 256 * 4 + 128 * 128 * 4;
}

// ---------------------------------------------------------------------------
// particles (CPU-written dynamic vertex buffer) and immediate-mode ribbons

static int buildParticles(VtxU* out, u32 frame, int n) {
	if (n > MAX_PART) n = MAX_PART;
	// camera right/up from the view matrix rows
	float rx = view.r[0].x, ry = view.r[0].y, rz = view.r[0].z;
	float ux = view.r[1].x, uy = view.r[1].y, uz = view.r[1].z;
	for (int i = 0; i < n; i++) {
		int src = i % 6;
		float sx = (src - 2.5f) * 12.0f, sz = ((src * 5) % 6 - 2.5f) * 10.0f;
		u32 life = (frame + i * 7) % 120;
		float tt = life * (1.0f / 60.0f);
		float vx = (hashf(i * 3) - 0.5f) * 6.0f, vz = (hashf(i * 3 + 1) - 0.5f) * 6.0f, vy = 8.0f + hashf(i * 3 + 2) * 6.0f;
		float px = sx + vx * tt, py = terrainH(sx, sz) + vy * tt - 4.9f * tt * tt, pz = sz + vz * tt;
		float s = 0.5f + tt * 0.4f;
		u8 a = (u8)(255 - life * 2);
		u32 h = hash32(i);
		float cx[4] = { -1, 1, 1, -1 }, cy[4] = { -1, -1, 1, 1 };
		static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
		for (int k = 0; k < 6; k++) {
			VtxU* v = &out[i * 6 + k];
			int c = idx[k];
			v->pos[0] = px + (rx * cx[c] + ux * cy[c]) * s;
			v->pos[1] = py + (ry * cx[c] + uy * cy[c]) * s;
			v->pos[2] = pz + (rz * cx[c] + uz * cy[c]) * s;
			v->tc[0] = cx[c] > 0 ? 1.0f : 0.0f;
			v->tc[1] = cy[c] > 0 ? 1.0f : 0.0f;
			v->clr[0] = 128 | (h & 0x7F); v->clr[1] = 128 | ((h >> 8) & 0x7F); v->clr[2] = 64 | ((h >> 16) & 0x3F); v->clr[3] = a;
		}
	}
	GSPGPU_FlushDataCache(out, sizeof(VtxU) * 6 * n);
	return n;
}

static void drawRibbons(u32 frame, int n) {
	for (int r = 0; r < n; r++) {
		C3D_ImmDrawBegin(GPU_TRIANGLE_STRIP);
		for (int s = 0; s < 50; s++) {
			float a = s * 0.12f + frame * 0.03f + r * 1.57f;
			float rad = 14.0f + 3.0f * sinf(a * 0.5f + r);
			float x = cosf(a) * rad, z = sinf(a) * rad, y = 6.0f + 2.0f * sinf(a * 2.0f + r) + r;
			for (int e = 0; e < 2; e++) {
				C3D_ImmSendAttrib(x, y + (e ? 0.8f : -0.8f), z, 1.0f);
				C3D_ImmSendAttrib(s * 0.1f, (float)e, 0.0f, 0.0f);
				C3D_ImmSendAttrib(255.0f, 160.0f + r * 20.0f, 80.0f, 200.0f - s * 3.0f);
			}
		}
		C3D_ImmDrawEnd();
		g_stats.draws++;
		g_stats.tris += 98;
	}
}

// ---------------------------------------------------------------------------

void r3d_draw_world(C3D_RenderTarget* target, u32 frame, const World3D* w) {
	float t = frame * (1.0f / 60.0f);
	float ang = frame * 0.004f;
	C3D_FVec eye = FVec3_New(cosf(ang) * 38.0f, 16.0f + 3.0f * sinf(ang * 2.3f), sinf(ang) * 38.0f);
	Mtx_LookAt(&view, eye, FVec3_New(0, 2, 0), FVec3_New(0, 1, 0), false);
	Mtx_PerspTilt(&proj, C3D_AngleFromDegrees(55.0f), C3D_AspectRatioTop, 0.5f, 140.0f, false);

	r3d_reset_state();
	if (w->rtt) rttPass(frame, t);

	C3D_FrameDrawOn(target);
	C3D_Mtx ident;
	Mtx_Identity(&ident);

	// --- opaque, per-vertex lit, fogged: terrain + props
	bindLit(&proj, 1.0f);
	C3D_FogGasMode(GPU_FOG, GPU_PLAIN_DENSITY, false);
	C3D_FogColor(0xD0B090);
	C3D_FogLutBind(&fogLut);
	C3D_TexBind(0, &texTerrain);
	for (int c = 0; c < w->terrainChunks && c < 16; c++) {
		setMV(uLit.mv, &ident);
		drawMesh(&mTerrain[c]);
	}
	drawProps(0, w->props, t, false);

	// --- trees: ETC1A4 cutouts, alpha test, no culling
	C3D_CullFace(GPU_CULL_NONE);
	C3D_AlphaTest(true, GPU_GREATER, 0x80);
	C3D_TexBind(0, &texLeaf);
	C3D_TexEnvColor(C3D_GetTexEnv(1), 0xFFFFFFFF);
	for (int g = 0; g < w->treeGroups && g < 6; g++) {
		setMV(uLit.mv, &ident);
		drawMesh(&mTrees[g]);
	}
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	C3D_CullFace(GPU_CULL_BACK_CCW);
	C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);

	// --- fragment-lit (PICA light env with LUTs): tori + hero spheres
	if (w->tori || w->heroes) {
		C3D_BindProgram(&pFrag);
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uFrag.proj, &proj);
		C3D_LightEnvBind(&lightEnv);
		C3D_FVec lp = Mtx_MultiplyFVec4(&view, FVec4_New(10.0f * cosf(t), 20.0f, 10.0f * sinf(t), 1.0f));
		C3D_LightPosition(&light, &lp);
		C3D_TexEnv* e = C3D_GetTexEnv(0);
		C3D_TexEnvInit(e);
		C3D_TexEnvSrc(e, C3D_Both, GPU_TEXTURE0, GPU_FRAGMENT_PRIMARY_COLOR, GPU_FRAGMENT_SECONDARY_COLOR);
		C3D_TexEnvFunc(e, C3D_RGB, GPU_MULTIPLY_ADD);
		C3D_TexEnvFunc(e, C3D_Alpha, GPU_MODULATE);
		e = C3D_GetTexEnv(1);
		C3D_TexEnvInit(e);
		C3D_TexEnvSrc(e, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT, 0);
		C3D_TexEnvFunc(e, C3D_RGB, GPU_MODULATE);
		C3D_TexBind(0, &texMetal);
		for (int i = 0; i < w->tori && i < 8; i++) {
			C3D_Mtx m;
			float a = i * (2.0f * M_PI / 8.0f) + t * 0.3f;
			Mtx_Identity(&m);
			Mtx_Translate(&m, cosf(a) * 20.0f, 5.0f + sinf(t + i), sinf(a) * 20.0f, true);
			Mtx_RotateX(&m, t + i, true);
			Mtx_RotateZ(&m, t * 0.5f, true);
			Mtx_Scale(&m, 2.0f, 2.0f, 2.0f);
			setMV(uFrag.mv, &m);
			C3D_TexEnvColor(e, 0xFF000000 | (hash32(i + 100) | 0x606060));
			drawMesh(&mTorus);
		}
		C3D_TexBind(0, &texNoise);
		for (int i = 0; i < w->heroes && i < 4; i++) {
			C3D_Mtx m;
			Mtx_Identity(&m);
			Mtx_Translate(&m, (i - 1.5f) * 8.0f, 4.0f + sinf(t * 2.0f + i), 4.0f, true);
			Mtx_RotateY(&m, t + i, true);
			Mtx_Scale(&m, 2.5f, 2.5f, 2.5f);
			setMV(uFrag.mv, &m);
			C3D_TexEnvColor(e, 0xFFFFFFFF);
			drawMesh(&mSphereHi);
		}
		C3D_LightEnvBind(NULL);
	}

	// --- RTT "monitor": 3 textures (rtt, history, bloom), 3-stage combiner
	if (w->rtt) {
		bindUnlit(&proj, vboU);
		C3D_CullFace(GPU_CULL_NONE);
		C3D_TexBind(0, &rttTex);
		C3D_TexBind(1, &histTex);
		C3D_TexBind(2, &bloomTex);
		C3D_TexEnv* e = C3D_GetTexEnv(0);
		C3D_TexEnvInit(e);
		C3D_TexEnvSrc(e, C3D_Both, GPU_TEXTURE0, GPU_TEXTURE1, GPU_CONSTANT);
		C3D_TexEnvOpRgb(e, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA);
		C3D_TexEnvFunc(e, C3D_Both, GPU_INTERPOLATE);
		C3D_TexEnvColor(e, 0xB0FFFFFF);
		e = C3D_GetTexEnv(1);
		C3D_TexEnvInit(e);
		C3D_TexEnvSrc(e, C3D_RGB, GPU_PREVIOUS, GPU_TEXTURE2, 0);
		C3D_TexEnvFunc(e, C3D_RGB, GPU_ADD);
		e = C3D_GetTexEnv(2);
		C3D_TexEnvInit(e);
		C3D_TexEnvSrc(e, C3D_Both, GPU_PREVIOUS, GPU_PRIMARY_COLOR, 0);
		C3D_TexEnvFunc(e, C3D_Both, GPU_MODULATE);
		te_reset(3);
		for (int k = 0; k < 2; k++) {
			C3D_Mtx m;
			Mtx_Identity(&m);
			Mtx_Translate(&m, k ? 12.0f : -12.0f, 2.0f, k ? -10.0f : 10.0f, true);
			Mtx_RotateY(&m, k ? 0.6f : -2.5f, true);
			setMV(uUnlit.mv, &m);
			drawArr(qMonitor, 6);
		}
		C3D_CullFace(GPU_CULL_BACK_CCW);
	}

	// --- translucent: props, water, particles, ribbons (depth write off)
	C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_COLOR);
	if (w->props) {
		bindLit(&proj, 0.55f);
		drawProps(0, w->props, t, true);
	}
	if (w->water) {
		bindUnlit(&proj, vboU);
		C3D_FogGasMode(GPU_FOG, GPU_PLAIN_DENSITY, false);
		C3D_FVUnifSet(GPU_VERTEX_SHADER, uUnlit.tcx1, 0.7f, 0.7f, t * 0.05f, t * 0.03f);
		C3D_TexBind(0, &texWater);
		C3D_TexBind(1, &texNoise);
		C3D_TexEnv* e = C3D_GetTexEnv(0);
		C3D_TexEnvInit(e);
		C3D_TexEnvSrc(e, C3D_Both, GPU_TEXTURE0, GPU_TEXTURE1, 0);
		C3D_TexEnvFunc(e, C3D_RGB, GPU_MODULATE);
		C3D_TexEnvFunc(e, C3D_Alpha, GPU_REPLACE);
		e = C3D_GetTexEnv(1);
		C3D_TexEnvInit(e);
		C3D_TexEnvSrc(e, C3D_Both, GPU_PREVIOUS, GPU_PRIMARY_COLOR, GPU_CONSTANT);
		C3D_TexEnvFunc(e, C3D_RGB, GPU_MULTIPLY_ADD);
		C3D_TexEnvFunc(e, C3D_Alpha, GPU_MODULATE);
		C3D_TexEnvColor(e, 0x60302010);
		e = C3D_GetTexEnv(2);
		C3D_TexEnvInit(e);
		C3D_TexEnvSrc(e, C3D_Alpha, GPU_PREVIOUS, GPU_CONSTANT, 0);
		C3D_TexEnvFunc(e, C3D_Alpha, GPU_MODULATE);
		C3D_TexEnvColor(e, 0xB0FFFFFF);
		te_reset(3);
		setMV(uUnlit.mv, &ident);
		drawArr(qWater, 6);
		C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);
	}
	if (w->particles) {
		VtxU* buf = dynU[frame & 1];
		int n = buildParticles(buf, frame, w->particles);
		bindUnlit(&proj, buf);
		setMV(uUnlit.mv, &ident);
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_SRC_ALPHA, GPU_ONE);
		C3D_CullFace(GPU_CULL_NONE);
		C3D_TexBind(0, &texGlow);
		C3D_TexEnv* e = C3D_GetTexEnv(0);
		C3D_TexEnvInit(e);
		C3D_TexEnvSrc(e, C3D_RGB, GPU_PRIMARY_COLOR, GPU_CONSTANT, 0);
		C3D_TexEnvFunc(e, C3D_RGB, GPU_MODULATE);
		C3D_TexEnvSrc(e, C3D_Alpha, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
		C3D_TexEnvFunc(e, C3D_Alpha, GPU_MODULATE);
		te_reset(1);
		int per = (n + 5) / 6;
		for (int b = 0; b * per < n; b++) {
			int cnt = n - b * per < per ? n - b * per : per;
			static const u32 tints[6] = { 0xFFFFFFFF, 0xFFC0E0FF, 0xFFFFC0C0, 0xFFC0FFC0, 0xFFFFFFC0, 0xFFE0C0FF };
			C3D_TexEnvColor(e, tints[b % 6]);
			drawArr(b * per * 6, cnt * 6);
		}
	}
	if (w->ribbons) {
		bindUnlit(&proj, vboU);
		setMV(uUnlit.mv, &ident);
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
		C3D_CullFace(GPU_CULL_NONE);
		C3D_TexBind(0, &texNoise);
		C3D_TexEnv* e = C3D_GetTexEnv(0);
		C3D_TexEnvInit(e);
		C3D_TexEnvSrc(e, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
		C3D_TexEnvFunc(e, C3D_Both, GPU_MODULATE);
		te_reset(1);
		drawRibbons(frame, w->ribbons);
	}
	r3d_reset_state();
}

// ---------------------------------------------------------------------------
// fill-rate scene: full-screen multi-texture alpha-blended layers

void r3d_draw_fill(C3D_RenderTarget* target, bool top, u32 frame, int layers) {
	float t = frame * (1.0f / 60.0f);
	C3D_FrameDrawOn(target);
	r3d_reset_state();
	C3D_Mtx p, ident;
	Mtx_OrthoTilt(&p, 0.0f, top ? 400.0f : 320.0f, 0.0f, 240.0f, -1.0f, 1.0f, true);
	Mtx_Identity(&ident);
	bindUnlit(&p, vboU);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uUnlit.mv, &ident);
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	C3D_CullFace(GPU_CULL_NONE);

	C3D_Tex* set[6] = { &texNoise, &texMetal, &texWater, &texChecker, &texTerrain, &texBrick };
	for (int l = 0; l < layers; l++) {
		float a = t * (0.2f + l * 0.07f) + l;
		C3D_FVUnifSet(GPU_VERTEX_SHADER, uUnlit.tcx1, 1.0f + 0.3f * sinf(a), 1.0f + 0.3f * cosf(a), sinf(a * 0.7f), cosf(a * 0.9f));
		C3D_FVUnifSet(GPU_VERTEX_SHADER, uUnlit.tcx2, 0.5f, 0.5f, t * 0.1f * (l + 1), -t * 0.05f);
		C3D_TexBind(0, set[l % 6]);
		C3D_TexBind(1, set[(l + 1) % 6]);
		C3D_TexBind(2, set[(l + 3) % 6]);

		// stage0: tex0 * tex1; stage1: (prev * primary) + const; stage2: lerp(prev, tex2, const.a);
		// stage3: alpha = tex0.a * const.a
		C3D_TexEnv* e = C3D_GetTexEnv(0);
		C3D_TexEnvInit(e);
		C3D_TexEnvSrc(e, C3D_Both, GPU_TEXTURE0, GPU_TEXTURE1, 0);
		C3D_TexEnvFunc(e, C3D_Both, GPU_MODULATE);
		e = C3D_GetTexEnv(1);
		C3D_TexEnvInit(e);
		C3D_TexEnvSrc(e, C3D_RGB, GPU_PREVIOUS, GPU_PRIMARY_COLOR, GPU_CONSTANT);
		C3D_TexEnvFunc(e, C3D_RGB, GPU_MULTIPLY_ADD);
		C3D_TexEnvColor(e, 0xFF000000 | (hash32(l) & 0x3F3F3F));
		e = C3D_GetTexEnv(2);
		C3D_TexEnvInit(e);
		C3D_TexEnvSrc(e, C3D_RGB, GPU_PREVIOUS, GPU_TEXTURE2, GPU_CONSTANT);
		C3D_TexEnvOpRgb(e, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA);
		C3D_TexEnvFunc(e, C3D_RGB, GPU_INTERPOLATE);
		C3D_TexEnvColor(e, 0x80FFFFFF);
		e = C3D_GetTexEnv(3);
		C3D_TexEnvInit(e);
		C3D_TexEnvSrc(e, C3D_Alpha, GPU_TEXTURE0, GPU_CONSTANT, 0);
		C3D_TexEnvFunc(e, C3D_Alpha, GPU_MODULATE);
		C3D_TexEnvColor(e, 0x90FFFFFF);
		te_reset(4);
		if (l & 1)
			C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_ZERO, GPU_ONE);
		else
			C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
		drawArr(top ? qFullTop : qFullBot, 6);
	}
	r3d_reset_state();
}
