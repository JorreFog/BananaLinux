// A stand-in game for the sway tests (rocknix/tests/home-sway.sh): an SDL2 client that draws a new frame every 20 ms,
// fullscreen on one output or on both (a two-screen emulator's two windows, the second titled "... Bottom" as
// ROCKNIX's rule wants). Each frame's number is in its picture, so a screenshot says which frame was on screen: the
// left half of every window is the colour (n & 255, (n >> 8) & 255, 0x5a), the right half a marker colour (green on
// the top window, blue on the bottom one). The number is also written to <counter file> as it is drawn, so a test
// sees whether the game is running or frozen.
//   sway-game <counter file> [both] [display index of the top]
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void paint(SDL_Window* w, unsigned n, Uint32 marker)
{
	SDL_Surface* s = SDL_GetWindowSurface(w);
	if (!s) return;
	SDL_Rect l = { 0, 0, s->w / 2, s->h }, r = { s->w / 2, 0, s->w - s->w / 2, s->h };
	SDL_FillRect(s, &l, SDL_MapRGB(s->format, n & 255, (n >> 8) & 255, 0x5a));
	SDL_FillRect(s, &r, marker == 0 ? SDL_MapRGB(s->format, 0, 200, 0) : SDL_MapRGB(s->format, 0, 0, 200));
	SDL_UpdateWindowSurface(w);
}

int main(int argc, char** argv)
{
	if (argc < 2) { fprintf(stderr, "usage: sway-game <counter file> [both] [top display]\n"); return 2; }
	const int both = argc > 2 && !strcmp(argv[2], "both");
	const int top = argc > 3 ? atoi(argv[3]) : 0;
	if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "sway-game: %s\n", SDL_GetError()); return 1; }
	SDL_Window* w[2] = { NULL, NULL };
	const int n = SDL_GetNumVideoDisplays();
	for (int i = 0; i < (both ? 2 : 1); i++)
	{
		const int d = i == 0 ? top : (n > 1 ? 1 - top : top);
		w[i] = SDL_CreateWindow(i == 0 ? "Game" : "Game Bottom", SDL_WINDOWPOS_CENTERED_DISPLAY(d), SDL_WINDOWPOS_CENTERED_DISPLAY(d),
			640, 480, SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_BORDERLESS);
		if (!w[i]) { fprintf(stderr, "sway-game: %s\n", SDL_GetError()); return 1; }
	}
	for (unsigned f = 1;; f++)
	{
		SDL_Event ev;
		while (SDL_PollEvent(&ev))
			if (ev.type == SDL_QUIT) return 0;
		for (int i = 0; i < 2; i++) if (w[i]) paint(w[i], f, i);
		// (written aside and renamed: a game frozen half way through a write leaves the last number whole)
		char tmp[4096];
		snprintf(tmp, sizeof tmp, "%s.new", argv[1]);
		FILE* c = fopen(tmp, "w");
		if (c) { fprintf(c, "%u\n", f); fclose(c); rename(tmp, argv[1]); }
		SDL_Delay(20);
	}
}
