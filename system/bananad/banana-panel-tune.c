/* banana-panel-tune: both panels as close to 60.000 Hz as their pixel clock allows.
 *
 * The RG DS's and RG DS Plus's panels are described in the device tree as text (the generic DSI panel driver's
 * panel_description: "M clock=<kHz> horizontal=<active>,<front porch>,<sync>,<back porch> vertical=..."). Their
 * refresh is the real pixel clock divided by the total pixels a frame, and the real clock is whatever the VOP can
 * divide out of its PLL, not always the clock asked for (the RG DS asks for 42134 kHz and gets 42133334 Hz). This
 * reads the real clock of each panel's VOP from the kernel (debugfs clk_summary, dclk_vop0 and dclk_vop1), finds
 * the front porches that bring the refresh closest to 60 Hz, and rewrites the timing in the boot DTB in place.
 * Each number keeps its digit count, so the DTB keeps its size and layout (no dtc needed), as ROCKNIXDS's 60 Hz
 * fix does. It takes effect on the next boot.
 *
 *   banana-panel-tune <dtb> [--dry-run] [--dclk HZ]    (--dclk: use this clock instead of reading the kernel's)
 *
 * Exits 0 with a report on stdout; the DTB is only written when the result is closer to 60 Hz than it was.
 */
#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct timing { long clock; int h[4], v[4]; char *at; size_t len; };

static int digits(int v) { int n = 1; while (v >= 10) { v /= 10; n++; } return n; }

static double refresh(double dclk, const int h[4], const int v[4])
{
	return dclk / ((double)(h[0] + h[1] + h[2] + h[3]) * (v[0] + v[1] + v[2] + v[3]));
}

/* the rate of a clock in clk_summary (Hz), 0 if it isn't there */
static double clk_rate(const char *name)
{
	FILE *f = fopen("/sys/kernel/debug/clk/clk_summary", "r");
	if (!f) return 0;
	char ln[512];
	double r = 0;
	while (fgets(ln, sizeof ln, f)) {
		char n[128];
		long en, prep, prot;
		double rate;
		/* "   dclk_vop0    1  1  0  42133334  0  0  50000  Y ..." */
		if (sscanf(ln, " %127s %ld %ld %ld %lf", n, &en, &prep, &prot, &rate) == 5 && !strcmp(n, name)) { r = rate; break; }
	}
	fclose(f);
	return r;
}

/* the next "M clock=... horizontal=a,b,c,d vertical=e,f,g,h" in buf, from p */
static char *next_timing(char *buf, size_t n, char *p, struct timing *t)
{
	for (; p && p < buf + n; p++) {
		p = memmem(p, buf + n - p, "M clock=", 8);
		if (!p) return NULL;
		int used = 0;
		if (sscanf(p, "M clock=%ld horizontal=%d,%d,%d,%d vertical=%d,%d,%d,%d%n", &t->clock, &t->h[0], &t->h[1], &t->h[2],
		           &t->h[3], &t->v[0], &t->v[1], &t->v[2], &t->v[3], &used) == 9) {
			t->at = p;
			t->len = used;
			return p + used;
		}
	}
	return NULL;
}

int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: banana-panel-tune <dtb> [--dry-run] [--dclk HZ]\n"); return 2; }
	int dry = 0;
	double forced = 0;
	for (int i = 2; i < argc; i++) {
		if (!strcmp(argv[i], "--dry-run")) dry = 1;
		else if (!strcmp(argv[i], "--dclk") && i + 1 < argc) forced = atof(argv[++i]);
	}
	FILE *f = fopen(argv[1], "rb");
	if (!f) { perror(argv[1]); return 1; }
	fseek(f, 0, SEEK_END);
	size_t n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = malloc(n + 1);
	if (!buf || fread(buf, 1, n, f) != n) { fprintf(stderr, "can't read %s\n", argv[1]); return 1; }
	buf[n] = 0;
	fclose(f);

	int changed = 0, panel = 0;
	struct timing t;
	for (char *p = buf; (p = next_timing(buf, n, p, &t)); panel++) {
		char vop[24];
		snprintf(vop, sizeof vop, "dclk_vop%d", panel);
		double dclk = forced > 0 ? forced : clk_rate(vop);
		const char *src = forced > 0 ? "given" : "measured";
		if (dclk <= 0) { dclk = t.clock * 1000.0; src = "asked for (no clk_summary)"; }
		double was = refresh(dclk, t.h, t.v);
		/* every front porch pair within reach that keeps each number's digit count: the smallest change that comes
		 * within 0.002 Hz of 60, else the closest. Porches far from the panel maker's are a risk worth taking only
		 * for a real gain. */
		int best[2] = { t.h[1], t.v[1] }, near[2] = { -1, -1 };
		double bestErr = fabs(was - 60);
		int nearCost = 1 << 30;
		for (int hf = t.h[1] * 3 / 4; hf <= t.h[1] * 5 / 4; hf++) {
			if (digits(hf) != digits(t.h[1])) continue;
			for (int vf = t.v[1] / 2 > 0 ? t.v[1] / 2 : 1; vf <= t.v[1] * 3; vf++) {
				if (digits(vf) != digits(t.v[1])) continue;
				int h[4] = { t.h[0], hf, t.h[2], t.h[3] }, v[4] = { t.v[0], vf, t.v[2], t.v[3] };
				double err = fabs(refresh(dclk, h, v) - 60);
				int cost = abs(hf - t.h[1]) + abs(vf - t.v[1]);
				if (err < 0.002 && cost < nearCost) { nearCost = cost; near[0] = hf; near[1] = vf; }
				if (err < bestErr - 1e-9) { bestErr = err; best[0] = hf; best[1] = vf; }
			}
		}
		if (near[0] >= 0) { best[0] = near[0]; best[1] = near[1]; }
		int h[4] = { t.h[0], best[0], t.h[2], t.h[3] }, v[4] = { t.v[0], best[1], t.v[2], t.v[3] };
		double now = refresh(dclk, h, v);
		printf("panel %d: %s pixel clock %.0f Hz, %d,%d,%d,%d x %d,%d,%d,%d = %.4f Hz", panel, src, dclk,
		       t.h[0], t.h[1], t.h[2], t.h[3], t.v[0], t.v[1], t.v[2], t.v[3], was);
		if ((best[0] == t.h[1] && best[1] == t.v[1]) || fabs(now - 60) > fabs(was - 60) - 0.005) {
			printf(": kept\n");
			continue;
		}
		printf(" -> front porches %d, %d = %.4f Hz\n", best[0], best[1], now);
		char repl[128];
		int len = snprintf(repl, sizeof repl, "M clock=%ld horizontal=%d,%d,%d,%d vertical=%d,%d,%d,%d", t.clock,
		                   h[0], h[1], h[2], h[3], v[0], v[1], v[2], v[3]);
		if ((size_t)len != t.len) { printf("panel %d: the new timing doesn't fit in place: kept\n", panel); continue; }
		memcpy(t.at, repl, len);
		changed++;
	}
	if (!panel) { printf("no panel timing in %s\n", argv[1]); return 1; }
	if (!changed || dry) return 0;

	/* beside it, then renamed over: a power cut leaves the old DTB or the new one */
	char tmp[4096];
	snprintf(tmp, sizeof tmp, "%s.new", argv[1]);
	f = fopen(tmp, "wb");
	if (!f || fwrite(buf, 1, n, f) != n || fflush(f) || fsync(fileno(f))) { perror(tmp); return 1; }
	fclose(f);
	if (rename(tmp, argv[1])) { perror("rename"); return 1; }
	sync();
	printf("written: %s (from the next boot)\n", argv[1]);
	return 0;
}
