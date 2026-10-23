// Audio: ndsp with a looping stereo PCM16 buffer and a looping mono DSP-ADPCM
// buffer, both synthesized (and ADPCM-encoded) at startup.
#include "bench.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define RATE      32728
#define PCM_LEN   (RATE)       // 1 s stereo loop
#define ADPCM_FRAMES 2338      // 2338 * 14 = 32732 samples (~1 s)

static bool ok;
static s16* pcm;
static u8* adpcm;
static ndspWaveBuf wbPcm, wbAdpcm;
static ndspAdpcmData adpcmCtx;
static u16 coefs[16];

static const s16 COEF[8][2] = {
	{ 0, 0 }, { 2048, 0 }, { 4096, -2048 }, { 3840, -1920 },
	{ 3584, -1664 }, { 3072, -1024 }, { 1024, 0 }, { 3968, -2016 },
};

static inline s16 clamp16(int v) { return v < -32768 ? -32768 : v > 32767 ? 32767 : (s16)v; }

// Simple DSP-ADPCM encoder: per 14-sample frame, try all predictors and pick the
// one with the smallest error, with the scale derived from the peak residual.
static void encodeAdpcm(const s16* in, int frames, u8* out, s16* h1o, s16* h2o) {
	int h1 = 0, h2 = 0;
	for (int f = 0; f < frames; f++) {
		const s16* s = in + f * 14;
		long bestErr = -1;
		u8 best[8];
		int bh1 = 0, bh2 = 0;
		for (int p = 0; p < 8; p++) {
			int c1 = COEF[p][0], c2 = COEF[p][1];
			// peak residual using the ideal (unquantised) history
			int peak = 0, t1 = h1, t2 = h2;
			for (int i = 0; i < 14; i++) {
				int pred = (c1 * t1 + c2 * t2 + 1024) >> 11;
				int r = s[i] - pred;
				if (r < 0) r = -r;
				if (r > peak) peak = r;
				t2 = t1; t1 = s[i];
			}
			int scale = 0;
			while (scale < 12 && (7 << scale) < peak) scale++;
			// real encode with decoder feedback
			u8 tmp[8];
			memset(tmp, 0, sizeof(tmp));
			tmp[0] = (u8)((p << 4) | scale);
			long err = 0;
			t1 = h1; t2 = h2;
			for (int i = 0; i < 14; i++) {
				int pred = c1 * t1 + c2 * t2;
				int diff = (s[i] << 11) - pred;
				int q = diff >= 0 ? (diff + (1 << (scale + 10))) >> (scale + 11) : -((-diff + (1 << (scale + 10))) >> (scale + 11));
				if (q > 7) q = 7;
				if (q < -8) q = -8;
				int dec = clamp16((((q << scale) << 11) + 1024 + pred) >> 11);
				int e = dec - s[i];
				err += (long)e * e;
				t2 = t1; t1 = dec;
				tmp[1 + i / 2] |= (u8)((q & 0xF) << ((i & 1) ? 0 : 4));
			}
			if (bestErr < 0 || err < bestErr) {
				bestErr = err;
				memcpy(best, tmp, 8);
				bh1 = t1; bh2 = t2;
			}
		}
		memcpy(out + f * 8, best, 8);
		h1 = bh1; h2 = bh2;
	}
	*h1o = (s16)h1;
	*h2o = (s16)h2;
}

// Minimal DSP1 header used only if no DSP firmware (sdmc:/3ds/dspfirm.cdc) is
// available. Emulators with HLE audio (Panda3DS, Citra) accept it; real
// hardware needs the real firmware dump.
static u8 fakeComponent[0x300] __attribute__((aligned(16)));

void audio_init(void) {
	Result rc = ndspInit();
	if (R_FAILED(rc)) {
#ifndef NO_FAKE_DSP
		memset(fakeComponent, 0, sizeof(fakeComponent));
		memcpy(fakeComponent + 0x100, "DSP1", 4);
		u32 sz = sizeof(fakeComponent);
		memcpy(fakeComponent + 0x104, &sz, 4);
		ndspUseComponent(fakeComponent, sizeof(fakeComponent), 0xFF, 0xFF);
		rc = ndspInit();
#endif
	}
	char msg[96];
	snprintf(msg, sizeof(msg), "BENCH audio init rc=%08lX", (unsigned long)rc);
	svcOutputDebugString(msg, strlen(msg));
	if (R_FAILED(rc)) return;
	ok = true;

	ndspSetOutputMode(NDSP_OUTPUT_STEREO);

	// stereo PCM16: a slowly beating chord with a tremolo, exactly 1 s so it loops cleanly
	pcm = (s16*)linearAlloc(PCM_LEN * 4);
	for (int i = 0; i < PCM_LEN; i++) {
		float t = (float)i / RATE;
		float env = 0.6f + 0.4f * sinf(2.0f * M_PI * 2.0f * t);
		float l = sinf(2.0f * M_PI * 220.0f * t) + 0.5f * sinf(2.0f * M_PI * 277.0f * t);
		float r = sinf(2.0f * M_PI * 330.0f * t) + 0.5f * sinf(2.0f * M_PI * 440.0f * t);
		pcm[i * 2 + 0] = (s16)(l * env * 6000.0f);
		pcm[i * 2 + 1] = (s16)(r * env * 6000.0f);
	}
	DSP_FlushDataCache(pcm, PCM_LEN * 4);

	// mono ADPCM: a plucked-string-ish waveform (saw with decaying harmonics)
	static s16 src[ADPCM_FRAMES * 14];
	for (int i = 0; i < ADPCM_FRAMES * 14; i++) {
		float t = (float)i / RATE;
		float ph = fmodf(t * 110.0f * 4.0f, 1.0f);
		float env = expf(-3.0f * fmodf(t * 4.0f, 1.0f));
		src[i] = (s16)((ph * 2.0f - 1.0f) * env * 9000.0f);
	}
	adpcm = (u8*)linearAlloc(ADPCM_FRAMES * 8);
	s16 h1, h2;
	encodeAdpcm(src, ADPCM_FRAMES, adpcm, &h1, &h2);
	DSP_FlushDataCache(adpcm, ADPCM_FRAMES * 8);
	for (int p = 0; p < 8; p++) {
		coefs[p * 2] = (u16)COEF[p][0];
		coefs[p * 2 + 1] = (u16)COEF[p][1];
	}

	float mix[12];
	memset(mix, 0, sizeof(mix));
	mix[0] = mix[1] = 0.8f;
	ndspChnReset(0);
	ndspChnSetInterp(0, NDSP_INTERP_POLYPHASE);
	ndspChnSetRate(0, RATE);
	ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
	ndspChnSetMix(0, mix);

	mix[0] = mix[1] = 0.6f;
	ndspChnReset(1);
	ndspChnSetInterp(1, NDSP_INTERP_LINEAR);
	ndspChnSetRate(1, RATE);
	ndspChnSetFormat(1, NDSP_FORMAT_MONO_ADPCM);
	ndspChnSetAdpcmCoefs(1, coefs);
	ndspChnSetMix(1, mix);

	memset(&wbPcm, 0, sizeof(wbPcm));
	wbPcm.data_pcm16 = pcm;
	wbPcm.nsamples = PCM_LEN;
	wbPcm.looping = true;

	adpcmCtx.index = adpcm[0];
	adpcmCtx.history0 = 0;
	adpcmCtx.history1 = 0;
	memset(&wbAdpcm, 0, sizeof(wbAdpcm));
	wbAdpcm.data_adpcm = adpcm;
	wbAdpcm.nsamples = ADPCM_FRAMES * 14;
	wbAdpcm.adpcm_data = &adpcmCtx;
	wbAdpcm.looping = true;
	(void)h1;
	(void)h2;
}

bool audio_ok(void) { return ok; }

void audio_play(bool on) {
	if (!ok) return;
	if (on) {
		ndspChnWaveBufClear(0);
		ndspChnWaveBufClear(1);
		wbPcm.status = NDSP_WBUF_FREE;
		wbAdpcm.status = NDSP_WBUF_FREE;
		ndspChnWaveBufAdd(0, &wbPcm);
		ndspChnWaveBufAdd(1, &wbAdpcm);
	} else {
		ndspChnWaveBufClear(0);
		ndspChnWaveBufClear(1);
	}
}

void audio_exit(void) {
	if (!ok) return;
	ndspChnWaveBufClear(0);
	ndspChnWaveBufClear(1);
	ndspExit();
}
