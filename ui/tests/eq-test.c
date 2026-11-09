/* The speaker EQ's arithmetic (eq.h), on the computer: cc -O2 -o eq-test eq-test.c -lm && ./eq-test
 *
 * The expected responses come from a separate implementation of the same RBJ filters (Python, outside this repo), so a
 * mistake in a formula here would not hide behind the same mistake in the test.
 */
#include <locale.h>
#include <stdio.h>
#include <time.h>
#include "eq/eq.h"

static int failed;
#define CHECK(cond, ...) do { if (!(cond)) { failed++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* the profile measured on a Retroid Pocket Nova's speakers, as the share kit exports it */
static const char nova[] =
	"Preamp: -15.3 dB\n"
	"Filter 1: ON HPQ Fc 150 Hz Q 0.707\n"
	"Filter 2: ON PK Fc 270 Hz Gain 5.0 dB Q 1.00\n"
	"Filter 3: ON PK Fc 840 Hz Gain -6.0 dB Q 1.30\n"
	"Filter 4: ON PK Fc 1335 Hz Gain -2.5 dB Q 4.00\n"
	"Filter 5: ON PK Fc 1750 Hz Gain -3.5 dB Q 3.50\n"
	"Filter 6: ON PK Fc 4100 Hz Gain -3.0 dB Q 2.00\n"
	"Filter 7: ON PK Fc 5100 Hz Gain -8.5 dB Q 2.00\n"
	"Filter 8: ON PK Fc 11000 Hz Gain 9.0 dB Q 0.90\n"
	"Filter 9: ON PK Fc 15000 Hz Gain 9.5 dB Q 0.80\n";

static struct eq_chain chain_of(const char *text, double rate, struct eq_profile *p)
{
	struct eq_profile tmp;
	if (!p) p = &tmp;
	eq_parse(text, strlen(text), p);
	struct eq_chain ch;
	eq_design(p, rate, &ch);
	return ch;
}

/* a sine through the chain, its level after the filters settle against before, in dB; clipped: samples that hit the rails */
static double sine_gain(const struct eq_chain *ch, double f, double amp, int *clipped)
{
	static short in[96000], out[96000];
	float z[EQ_MAX_BANDS * 2] = { 0 };
	for (int i = 0; i < 96000; i++) in[i] = (short)lrint(amp * sin(2 * M_PI * f * i / 48000));
	eq_run(ch, z, in, 1, out, 1, 96000);
	double si = 0, so = 0;
	int clip = 0;
	for (int i = 48000; i < 96000; i++) {	/* the second half: well past the filters' settling */
		si += (double)in[i] * in[i];
		so += (double)out[i] * out[i];
		if (out[i] == 32767 || out[i] == -32768) clip++;
	}
	if (clipped) *clipped = clip;
	return 10 * log10(so / si);
}

int main(void)
{
	struct eq_profile p;
	struct eq_chain ch = chain_of(nova, 48000, &p);

	/* parsing */
	CHECK(p.n == 9, "nova: %d filters, want 9", p.n);
	CHECK(fabs(p.preamp + 15.3) < 1e-9, "nova: preamp %g", p.preamp);
	CHECK(p.b[0].type == EQ_HP && p.b[0].f == 150 && fabs(p.b[0].q - 0.707) < 1e-9, "nova: the high-pass");
	CHECK(p.b[6].type == EQ_PK && p.b[6].f == 5100 && p.b[6].gain == -8.5 && p.b[6].q == 2, "nova: filter 7");

	/* the filters' response against the reference, and the preamp: the profile's own is enough, no headroom taken */
	static const double ref[][2] = { { 100, -7.083 }, { 150, -1.168 }, { 270, 4.138 }, { 840, -5.705 }, { 1335, -5.463 },
		{ 1750, -5.138 }, { 4100, -5.846 }, { 5100, -7.232 }, { 8000, 6.043 }, { 11000, 13.608 }, { 15000, 13.785 } };
	for (size_t i = 0; i < sizeof ref / sizeof *ref; i++) {
		double g = eq_response(&ch, ref[i][0], 48000);
		CHECK(fabs(g - ref[i][1]) < 0.02, "nova at %g Hz: %.3f dB, want %.3f", ref[i][0], g, ref[i][1]);
	}
	CHECK(ch.headroom == 0, "nova: headroom %g, want 0 (its peak is 14.64 dB, under its 15.3 dB preamp)", ch.headroom);
	CHECK(fabs(20 * log10(ch.gain) + 15.3) < 1e-4, "nova: gain %g", 20 * log10(ch.gain));

	/* what the samples do agrees with the arithmetic */
	for (size_t i = 0; i < sizeof ref / sizeof *ref; i++) {
		if (ref[i][0] < 200) continue;	/* (the high-pass's region: the 16-bit input's rounding is too near the result) */
		double g = sine_gain(&ch, ref[i][0], 20000, NULL);
		CHECK(fabs(g - (ref[i][1] - 15.3)) < 0.05, "nova, a sine at %g Hz: %.3f dB, want %.3f", ref[i][0], g, ref[i][1] - 15.3);
	}

	/* shelves, and their LS and HS spellings with no Q */
	ch = chain_of("Filter: ON LSC Fc 105 Hz Gain 5.5 dB Q 0.71\nFilter: ON HSC Fc 8000 Hz Gain -4.0 dB Q 0.71\n", 48000, &p);
	static const double sref[][2] = { { 50, 5.224 }, { 105, 2.750 }, { 1000, 0 }, { 8000, -2.000 }, { 16000, -3.953 } };
	for (size_t i = 0; i < sizeof sref / sizeof *sref; i++) {
		double g = eq_response(&ch, sref[i][0], 48000);
		CHECK(fabs(g - sref[i][1]) < 0.02, "shelves at %g Hz: %.3f dB, want %.3f", sref[i][0], g, sref[i][1]);
	}
	ch = chain_of("Filter: ON LS Fc 105 Hz Gain 5.5 dB\nFilter: ON HS Fc 8000 Hz Gain -4 dB\n", 48000, &p);
	CHECK(p.n == 2 && p.b[0].type == EQ_LSC && fabs(p.b[0].q - M_SQRT1_2) < 1e-9 && p.b[1].type == EQ_HSC, "LS and HS");
	CHECK(fabs(ch.headroom - 5.5) < 0.05, "shelves: headroom %.3f, want the low shelf's 5.5", ch.headroom);

	/* headroom: a 12 dB boost with no preamp is lowered by the curve's peak, and a full-scale sine there doesn't clip */
	ch = chain_of("Preamp: 0 dB\nFilter: ON PK Fc 1000 Hz Gain 12 dB Q 1\n", 48000, &p);
	CHECK(fabs(ch.headroom - 12) < 0.05, "boost: headroom %.3f, want 12", ch.headroom);
	int clipped = 0;
	double g = sine_gain(&ch, 1000, 32000, &clipped);
	CHECK(clipped == 0 && fabs(g) < 0.05, "boost: a full-scale 1 kHz sine came out at %.3f dB with %d samples clipped", g, clipped);
	/* a preamp lower than needed is kept as it is */
	ch = chain_of("Preamp: -20 dB\nFilter: ON PK Fc 1000 Hz Gain 12 dB Q 1\n", 48000, NULL);
	CHECK(ch.headroom == 0 && fabs(20 * log10(ch.gain) + 20) < 1e-4, "boost with its own preamp: %g, headroom %g", 20 * log10(ch.gain), ch.headroom);

	/* nothing to do: pass through untouched; a preamp alone is still a gain */
	ch = chain_of("", 48000, NULL);
	CHECK(!eq_active(&ch) && ch.gain == 1, "empty: active %d gain %g", eq_active(&ch), ch.gain);
	ch = chain_of("# just a comment\nFilter 1: OFF PK Fc 1000 Hz Gain 6 dB Q 1\n", 48000, NULL);
	CHECK(!eq_active(&ch), "only an OFF filter: active");
	ch = chain_of("Preamp: -6 dB\n", 48000, NULL);
	CHECK(eq_active(&ch) && ch.n == 0 && fabs(ch.gain - 0.501187) < 1e-5, "preamp alone: n %d gain %g", ch.n, ch.gain);

	/* lines it can't use are skipped, the rest kept */
	chain_of("Filter 1: ON XYZ Fc 1000 Hz Gain 6 dB Q 1\n"	/* unknown type */
		 "Filter 2: ON PK Fc 1000 Hz Gain 6 dB Q 0\n"	/* Q 0: unstable */
		 "Filter 3: ON PK Fc 1000 Hz Gain 99 dB Q 1\n"	/* no speaker means 99 dB */
		 "Filter 4: ON PK Fc 1000 Hz Gain 0 dB Q 1\n"	/* does nothing */
		 "Filter 5: ON PK Fc abc Hz Gain 6 dB Q 1\n"	/* no frequency */
		 "Filter 6: ON PK Fc 2000 Hz Gain -3 dB Q 1.5   # a comment after it\r\n"
		 "Preamp: lots\n", 48000, &p);
	CHECK(p.n == 1 && p.b[0].f == 2000 && p.b[0].gain == -3 && p.b[0].q == 1.5 && p.preamp == 0, "skipped lines: kept %d", p.n);
	/* at most EQ_MAX_BANDS */
	char many[4096] = "";
	for (int i = 0; i < 40; i++) {
		char ln[80];
		snprintf(ln, sizeof ln, "Filter %d: ON PK Fc %d Hz Gain -1 dB Q 1\n", i + 1, 100 + 100 * i);
		strcat(many, ln);
	}
	chain_of(many, 48000, &p);
	CHECK(p.n == EQ_MAX_BANDS, "40 filters: kept %d, want %d", p.n, EQ_MAX_BANDS);
	/* a filter at or past the Nyquist frequency is left out of the chain, not made into an unstable one */
	ch = chain_of("Filter: ON PK Fc 30000 Hz Gain -3 dB Q 1\nFilter: ON PK Fc 1000 Hz Gain -3 dB Q 1\n", 48000, &p);
	CHECK(p.n == 2 && ch.n == 1, "past Nyquist: %d in the profile, %d in the chain", p.n, ch.n);

	/* a decimal comma locale in the program doesn't change how the profile reads (strtod would have) */
	if (setlocale(LC_NUMERIC, "de_DE.UTF-8") || setlocale(LC_NUMERIC, "de_DE") || setlocale(LC_NUMERIC, "fr_FR.UTF-8")) {
		chain_of(nova, 48000, &p);
		CHECK(p.n == 9 && fabs(p.preamp + 15.3) < 1e-9 && p.b[1].q == 1 && fabs(p.b[0].q - 0.707) < 1e-9, "decimal comma locale");
		setlocale(LC_NUMERIC, "C");
	} else
		printf("(no decimal comma locale here: that check skipped)\n");

	/* silence after sound leaves no denormals in the state */
	{
		ch = chain_of(nova, 48000, NULL);
		float z[EQ_MAX_BANDS * 2] = { 0 };
		static short s[48000], o[48000];
		for (int i = 0; i < 4800; i++) s[i] = (short)(10000 * sin(i * 0.1));
		for (int i = 4800; i < 48000; i++) s[i] = 0;
		for (int i = 0; i < 48000; i += 480) eq_run(&ch, z, s + i, 1, o + i, 1, 480);
		int tiny = 0;
		for (int k = 0; k < ch.n * 2; k++) if (z[k] != 0 && fabsf(z[k]) < 1e-15f) tiny++;
		CHECK(tiny == 0 && o[47999] == 0, "denormals after a silence: %d, last sample %d", tiny, o[47999]);
	}

	/* any profile the parser accepts makes a stable chain: every filter's poles inside the unit circle with the float
	 * coefficients it runs on, and white noise in gives numbers out (a wild profile's middle stages can be large, which
	 * float holds; what matters is that nothing grows without end) */
	srand(1);
	for (int t = 0; t < 300; t++) {
		char txt[2048] = "";
		int nb = 1 + rand() % EQ_MAX_BANDS;
		for (int i = 0; i < nb; i++) {
			static const char *ty[] = { "PK", "LSC", "HSC", "LPQ", "HPQ" };
			char ln[120];
			snprintf(ln, sizeof ln, "Filter: ON %s Fc %.1f Hz Gain %.1f dB Q %.3f\n", ty[rand() % 5],
				 20 * pow(1000, rand() / (double)RAND_MAX), -30 + 60.0 * rand() / RAND_MAX, 0.06 + 20.0 * rand() / RAND_MAX);
			strcat(txt, ln);
		}
		ch = chain_of(txt, 48000, NULL);
		float z[EQ_MAX_BANDS * 2] = { 0 };
		static short s[24000], o[24000];
		for (int i = 0; i < 24000; i++) s[i] = (short)(rand() % 65536 - 32768);
		eq_run(&ch, z, s, 1, o, 1, 24000);
		int bad = 0;
		for (int k = 0; k < ch.n; k++) {
			float a1 = ch.c[k].a1, a2 = ch.c[k].a2;
			if (!(fabsf(a2) < 1 && fabsf(a1) < 1 + a2)) bad++;
		}
		for (int k = 0; k < ch.n * 2; k++) if (!isfinite(z[k])) bad++;
		CHECK(!bad, "random profile %d: unstable (%s)", t, txt);
	}

	/* a profile changed while sound plays (the plugin's reload, eq_carry): a filter added ahead of others (a slider
	 * moved off 0 under kept ones) is a change of tone, not a burst over the sound */
	{
		static const char a[] = "Filter: ON HPQ Fc 220 Hz Q 0.707\nFilter: ON PK Fc 982 Hz Gain -10 dB Q 1.2\n"
			"Filter: ON PK Fc 500 Hz Gain 3 dB Q 1.41\nFilter: ON PK Fc 8000 Hz Gain -3 dB Q 1.41\n";
		static const char b[] = "Filter: ON HPQ Fc 220 Hz Q 0.707\nFilter: ON PK Fc 982 Hz Gain -10 dB Q 1.2\n"
			"Filter: ON PK Fc 250 Hz Gain -1 dB Q 1.41\nFilter: ON PK Fc 500 Hz Gain 3 dB Q 1.41\nFilter: ON PK Fc 8000 Hz Gain -3 dB Q 1.41\n";
		struct eq_chain ca = chain_of(a, 48000, &p), cb = chain_of(b, 48000, &p);
		static short in[96000], o[96000];
		for (int i = 0; i < 96000; i++) in[i] = (short)(6000 * sin(2 * M_PI * 440 * i / 48000.0) + 3000 * sin(2 * M_PI * 3000 * i / 48000.0));
		int steady = 0, after = 0;
		float z[EQ_MAX_BANDS * 2] = { 0 };
		eq_run(&cb, z, in, 1, o, 1, 96000);
		for (int i = 48000; i < 96000; i++) if (abs(o[i]) > steady) steady = abs(o[i]);
		memset(z, 0, sizeof z);
		eq_run(&ca, z, in, 1, o, 1, 48000);
		eq_carry(&ca, &cb, z);
		eq_run(&cb, z, in + 48000, 1, o + 48000, 1, 48000);
		for (int i = 48000; i < 96000; i++) if (abs(o[i]) > after) after = abs(o[i]);
		CHECK(after <= steady * 1.05, "a filter added ahead of others while playing: peak %d, the new profile's own %d", after, steady);
		memset(z, 1, sizeof z);	/* (any state) */
		eq_carry(&cb, &ca, z);
		int rest = 1;
		for (int k = ca.n; k < EQ_MAX_BANDS; k++) rest = rest && z[2 * k] == 0 && z[2 * k + 1] == 0;
		CHECK(rest, "a filter gone: the state past the new chain isn't at rest");
	}

	/* a rough speed figure for this computer: the Nova profile on a minute of stereo */
	{
		ch = chain_of(nova, 48000, NULL);
		static short buf[48000 * 2];
		float z[2][EQ_MAX_BANDS * 2] = { { 0 } };
		for (int i = 0; i < 96000; i++) buf[i] = (short)(rand() % 20000 - 10000);
		struct timespec a, b;
		clock_gettime(CLOCK_MONOTONIC, &a);
		for (int sec = 0; sec < 60; sec++)
			for (int c = 0; c < 2; c++) eq_run(&ch, z[c], buf + c, 2, buf + c, 2, 48000);
		clock_gettime(CLOCK_MONOTONIC, &b);
		double ms = (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
		printf("speed: a minute of 48 kHz stereo through 9 filters in %.1f ms (%.3f%% of one core)\n", ms, ms / 600.0);
	}

	printf(failed ? "%d FAILED\n" : "all passed\n", failed);
	return failed != 0;
}
