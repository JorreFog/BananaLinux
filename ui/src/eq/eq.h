/* The speaker EQ's arithmetic, shared by the ALSA plugin (pcm_banana_eq.c) and its test (tests/eq-test.c).
 *
 * A profile is text in Equalizer APO's parametric format, the one AutoEq and most measurement tools export:
 *
 *   Preamp: -6.3 dB
 *   Filter 1: ON PK Fc 840 Hz Gain -6.0 dB Q 1.30
 *   Filter 2: ON HPQ Fc 150 Hz Q 0.707
 *
 * Types: PK (peak), LSC and HSC (shelves; LS and HS are the same with Q 0.707), LP and HP (Q 0.707), LPQ and HPQ.
 * A filter marked OFF, a line it doesn't know and anything after a '#' are skipped; at most EQ_MAX_BANDS are kept.
 * The filters are RBJ's cookbook biquads, the ones APO, PipeWire and the measurement tools use, so a profile sounds
 * the same here as where it was made.
 *
 * Headroom: a boost can push a full-scale sound past what 16 bits hold, and that clips (a harsh crackle, worst in the
 * loudest moments). eq_design takes the profile's preamp, then lowers it further if the whole curve still peaks above
 * 0 dB anywhere between 20 Hz and the Nyquist frequency, so no profile can clip a sound that didn't clip before.
 *
 * No allocation and no I/O here: the plugin calls these from the audio thread.
 */
#ifndef BANANA_EQ_H
#define BANANA_EQ_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* (a long AutoEq profile is 10 to 20 filters, and Settings adds its 7 sliders after it) */
#define EQ_MAX_BANDS 32

enum eq_type { EQ_PK, EQ_LSC, EQ_HSC, EQ_LP, EQ_HP };

struct eq_band {
	int type;
	double f, gain, q;
};

struct eq_profile {
	double preamp;		/* dB, as the profile gives it */
	int n;
	struct eq_band b[EQ_MAX_BANDS];
};

struct eq_coef {
	float b0, b1, b2, a1, a2;
};

struct eq_chain {
	int n;			/* 0: nothing to do, the samples pass through untouched */
	float gain;		/* linear: the preamp and the headroom, applied before the filters */
	double headroom;	/* dB the curve's peak took off the preamp (0 when the profile's own was enough) */
	struct eq_coef c[EQ_MAX_BANDS];
	struct eq_band band[EQ_MAX_BANDS];	/* what each filter is, for eq_carry */
};

/* a decimal number ("-6.25", "1e3" isn't one) without strtod, which reads the process's locale: in a game that set
 * one with a decimal comma, "1.30" would have been 1 */
static double eq_num(const char *s, const char **end)
{
	const char *p = s;
	while (*p == ' ' || *p == '\t') p++;
	double sign = 1, v = 0, scale = 1;
	if (*p == '-' || *p == '+') sign = *p++ == '-' ? -1 : 1;
	const char *digits = p;
	while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
	if (*p == '.') {
		p++;
		while (*p >= '0' && *p <= '9') { scale /= 10; v += (*p++ - '0') * scale; }
	}
	if (p == digits || (p == digits + 1 && *digits == '.')) { *end = s; return NAN; }
	*end = p;
	return sign * v;
}

/* a number after a word in a line ("Fc 840 Hz": eq_after(s, "Fc")), or NAN */
static double eq_after(const char *s, const char *word)
{
	size_t n = strlen(word);
	for (const char *p = s; *p; p++) {
		if (strncasecmp(p, word, n) || (p > s && p[-1] != ' ' && p[-1] != '\t')) continue;
		if (p[n] != ' ' && p[n] != '\t' && p[n] != ':') continue;
		const char *end;
		double v = eq_num(p + n + 1, &end);
		if (end != p + n + 1) return v;
	}
	return NAN;
}

/* the profile in text (len bytes, not necessarily NUL-terminated); returns how many filters it kept */
static int eq_parse(const char *text, size_t len, struct eq_profile *p)
{
	memset(p, 0, sizeof *p);
	char ln[256];
	size_t i = 0;
	while (i < len) {
		size_t k = 0;
		while (i < len && text[i] != '\n') {
			if (k < sizeof ln - 1) ln[k++] = text[i];
			i++;
		}
		i++;
		ln[k] = 0;
		char *hash = strchr(ln, '#');
		if (hash) *hash = 0;
		char *s = ln;
		while (*s == ' ' || *s == '\t') s++;
		if (!strncasecmp(s, "Preamp:", 7)) {
			const char *end;
			double v = eq_num(s + 7, &end);
			if (end != s + 7) p->preamp = v;
			continue;
		}
		if (strncasecmp(s, "Filter", 6)) continue;
		char *colon = strchr(s, ':');
		if (!colon || p->n >= EQ_MAX_BANDS) continue;
		char on[8] = "", type[8] = "";
		if (sscanf(colon + 1, " %7s %7s", on, type) != 2 || strcasecmp(on, "ON")) continue;
		struct eq_band b = { 0, eq_after(colon, "Fc"), eq_after(colon, "Gain"), eq_after(colon, "Q") };
		if (!strcasecmp(type, "PK") || !strcasecmp(type, "PEQ")) b.type = EQ_PK;
		else if (!strcasecmp(type, "LSC") || !strcasecmp(type, "LS")) b.type = EQ_LSC;
		else if (!strcasecmp(type, "HSC") || !strcasecmp(type, "HS")) b.type = EQ_HSC;
		else if (!strcasecmp(type, "LP") || !strcasecmp(type, "LPQ")) b.type = EQ_LP;
		else if (!strcasecmp(type, "HP") || !strcasecmp(type, "HPQ")) b.type = EQ_HP;
		else continue;
		if (isnan(b.q)) b.q = M_SQRT1_2;
		if (isnan(b.gain)) b.gain = 0;
		/* a frequency, a Q and a gain a speaker can't mean (and that would make the filter unstable) */
		if (!(b.f > 1 && b.f < 100000) || !(b.q > 0.05 && b.q < 50) || !(fabs(b.gain) <= 30)) continue;
		if ((b.type == EQ_PK || b.type == EQ_LSC || b.type == EQ_HSC) && b.gain == 0) continue;
		p->b[p->n++] = b;
	}
	if (!(fabs(p->preamp) <= 60)) p->preamp = 0;
	return p->n;
}

static void eq_coef_of(const struct eq_band *b, double rate, struct eq_coef *c)
{
	double w = 2 * M_PI * b->f / rate, cw = cos(w), sw = sin(w), alpha = sw / (2 * b->q);
	double A = pow(10, b->gain / 40), b0, b1, b2, a0, a1, a2;
	switch (b->type) {
	case EQ_LSC: {
		double r = 2 * sqrt(A) * alpha;
		b0 = A * ((A + 1) - (A - 1) * cw + r); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - r);
		a0 = (A + 1) + (A - 1) * cw + r; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - r;
		break;
	}
	case EQ_HSC: {
		double r = 2 * sqrt(A) * alpha;
		b0 = A * ((A + 1) + (A - 1) * cw + r); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - r);
		a0 = (A + 1) - (A - 1) * cw + r; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - r;
		break;
	}
	case EQ_LP:
		b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
		break;
	case EQ_HP:
		b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
		break;
	default:
		b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
	}
	c->b0 = (float)(b0 / a0); c->b1 = (float)(b1 / a0); c->b2 = (float)(b2 / a0);
	c->a1 = (float)(a1 / a0); c->a2 = (float)(a2 / a0);
}

/* the chain's gain in dB at f (the filters only, without the preamp). The filters' power gains are multiplied and
 * one log taken: eq_design's headroom sweep runs this ~700 times on the audio thread when a profile changes, and a log
 * per filter made that ~1 ms on the A55 for 14 filters. (32 filters each between -300 and +30 dB stay well inside a
 * double.) */
static double eq_response(const struct eq_chain *ch, double f, double rate)
{
	double w = 2 * M_PI * f / rate, c1 = cos(w), s1 = sin(w), c2 = cos(2 * w), s2 = sin(2 * w), pw = 1;
	for (int i = 0; i < ch->n; i++) {
		const struct eq_coef *k = &ch->c[i];
		double nr = k->b0 + k->b1 * c1 + k->b2 * c2, ni = -(k->b1 * s1 + k->b2 * s2);
		double dr = 1 + k->a1 * c1 + k->a2 * c2, di = -(k->a1 * s1 + k->a2 * s2);
		pw *= (nr * nr + ni * ni + 1e-30) / (dr * dr + di * di + 1e-30);
	}
	return 10 * log10(pw + 1e-300);
}

/* the profile's filters for this sample rate; a filter at or past the Nyquist frequency is left out */
static void eq_design(const struct eq_profile *p, double rate, struct eq_chain *ch)
{
	memset(ch, 0, sizeof *ch);
	ch->gain = 1;
	for (int i = 0; i < p->n; i++)
		if (p->b[i].f < rate * 0.49) {
			ch->band[ch->n] = p->b[i];
			eq_coef_of(&p->b[i], rate, &ch->c[ch->n++]);
		}
	if (!ch->n && p->preamp == 0) return;
	double peak = ch->n ? -1e9 : 0;	/* (no filters: flat) */
	for (double f = 20; ch->n && f < rate * 0.49; f *= 1.01) {
		double g = eq_response(ch, f, rate);
		if (g > peak) peak = g;
	}
	double pre = p->preamp;
	if (pre + peak > 0) {
		ch->headroom = pre + peak;
		pre = -peak;
	}
	ch->gain = (float)pow(10, pre / 20);
}

/* one channel's filter state (z, two floats a filter, EQ_MAX_BANDS of them) from the chain it ran with (old) to the new
 * one: a filter still there (the same type, frequency and Q: only its gain moved) keeps its state wherever it now is in
 * the chain, so a change while sound plays is a change of tone, not a click; a new filter starts at rest. (Kept by
 * position, a filter added or gone ahead of others, a slider moved to or from 0, ran the next one on another filter's
 * state: a burst up to 3 dB over the sound.) */
static void eq_carry(const struct eq_chain *old, const struct eq_chain *ch, float *z)
{
	float was[EQ_MAX_BANDS * 2];
	int used[EQ_MAX_BANDS] = { 0 };
	memcpy(was, z, sizeof was);
	for (int k = 0; k < EQ_MAX_BANDS; k++) {
		int j = -1;
		for (int t = -1; k < ch->n && j < 0 && t < old->n; t++) {
			const int i = t < 0 ? k : t;	/* (where it was first: the usual case) */
			const struct eq_band *a = &old->band[i], *b = &ch->band[k];
			if (i < old->n && !used[i] && a->type == b->type && a->f == b->f && a->q == b->q) j = i;
		}
		if (j >= 0) {
			used[j] = 1;
			z[2 * k] = was[2 * j];
			z[2 * k + 1] = was[2 * j + 1];
		} else
			z[2 * k] = z[2 * k + 1] = 0;
	}
}

static inline int eq_active(const struct eq_chain *ch)
{
	return ch->n > 0 || ch->gain != 1;
}

/* frames of one channel: in and out may be the same samples; the steps are in samples (2 for interleaved stereo); z is
 * the channel's state, two floats per filter. The filters are the transposed direct form II, in float: at 48 kHz the
 * lowest a speaker needs (a 150 Hz high-pass) stays well inside float's precision. */
static void eq_run(const struct eq_chain *ch, float *z, const short *in, size_t in_step, short *out, size_t out_step, size_t frames)
{
	const float g = ch->gain;
	for (size_t i = 0; i < frames; i++) {
		float x = in[i * in_step] * g;
		for (int k = 0; k < ch->n; k++) {
			const struct eq_coef *c = &ch->c[k];
			float y = c->b0 * x + z[2 * k];
			z[2 * k] = c->b1 * x - c->a1 * y + z[2 * k + 1];
			z[2 * k + 1] = c->b2 * x - c->a2 * y;
			x = y;
		}
		if (x > 32767.f) x = 32767.f;
		else if (x < -32768.f) x = -32768.f;
		out[i * out_step] = (short)lrintf(x);
	}
	/* a silence decays the state towards numbers too small for the FPU's fast path (denormals): zero them once a
	 * block, which costs nothing and keeps a quiet moment as cheap as a loud one */
	for (int k = 0; k < 2 * ch->n; k++)
		if (fabsf(z[k]) < 1e-15f) z[k] = 0;
}

#endif
