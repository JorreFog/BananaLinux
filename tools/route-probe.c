// route-probe: what the ALSA mixer API makes of the RPC's DSP route switches, on the handheld.
//   route-probe [name] [0|1]      (default: the speaker route, on)
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv)
{
	const char* name = argc > 1 ? argv[1] : "SENARY_MI2S_RX Audio Mixer MultiMedia1";
	int want = argc > 2 ? atoi(argv[2]) : 1;
	int r;
	snd_mixer_t* mx = NULL;
	r = snd_mixer_open(&mx, 0);
	printf("mixer_open   : %d %s\n", r, r < 0 ? snd_strerror(r) : "");
	if (r < 0) return 1;
	r = snd_mixer_attach(mx, "hw:0");
	printf("attach hw:0  : %d %s\n", r, r < 0 ? snd_strerror(r) : "");
	r = snd_mixer_selem_register(mx, NULL, NULL);
	printf("register     : %d %s\n", r, r < 0 ? snd_strerror(r) : "");
	r = snd_mixer_load(mx);
	printf("load         : %d %s\n", r, r < 0 ? snd_strerror(r) : "");
	printf("elements     : %d\n", snd_mixer_get_count(mx));

	snd_mixer_selem_id_t* sid = NULL;
	snd_mixer_selem_id_alloca(&sid);
	snd_mixer_selem_id_set_index(sid, 0);
	snd_mixer_selem_id_set_name(sid, name);
	snd_mixer_elem_t* e = snd_mixer_find_selem(mx, sid);
	printf("find_selem   : %s (%p)\n", e ? "found" : "NOT FOUND", (void*)e);
	if (!e)
	{
		printf("-- the playback switches this mixer does have --\n");
		for (snd_mixer_elem_t* it = snd_mixer_first_elem(mx); it; it = snd_mixer_elem_next(it))
		{
			if (!snd_mixer_selem_is_active(it) || !snd_mixer_selem_has_playback_switch(it)) continue;
			snd_mixer_selem_id_t* id = NULL;
			snd_mixer_selem_id_alloca(&id);
			snd_mixer_selem_get_id(it, id);
			printf("   [%s] idx=%u\n", snd_mixer_selem_id_get_name(id), snd_mixer_selem_id_get_index(id));
		}
		return 1;
	}
	printf("has_playback_switch: %d  joined: %d\n", snd_mixer_selem_has_playback_switch(e), snd_mixer_selem_has_playback_switch_joined(e));
	int fl = -1, fr = -1;
	snd_mixer_selem_get_playback_switch(e, SND_MIXER_SCHN_FRONT_LEFT, &fl);
	snd_mixer_selem_get_playback_switch(e, SND_MIXER_SCHN_FRONT_RIGHT, &fr);
	printf("before       : left=%d right=%d\n", fl, fr);

	r = snd_mixer_selem_set_playback_switch(e, SND_MIXER_SCHN_FRONT_LEFT, want);
	printf("set per-chan : %d %s\n", r, r < 0 ? snd_strerror(r) : "");
	snd_mixer_selem_get_playback_switch(e, SND_MIXER_SCHN_FRONT_LEFT, &fl);
	snd_mixer_selem_get_playback_switch(e, SND_MIXER_SCHN_FRONT_RIGHT, &fr);
	printf("  now        : left=%d right=%d\n", fl, fr);

	r = snd_mixer_selem_set_playback_switch_all(e, want);
	printf("set _all     : %d %s\n", r, r < 0 ? snd_strerror(r) : "");
	snd_mixer_selem_get_playback_switch(e, SND_MIXER_SCHN_FRONT_LEFT, &fl);
	snd_mixer_selem_get_playback_switch(e, SND_MIXER_SCHN_FRONT_RIGHT, &fr);
	printf("  now        : left=%d right=%d\n", fl, fr);

	// a fresh handle, the way a new process (amixer) would see it
	snd_mixer_t* mx2 = NULL;
	if (snd_mixer_open(&mx2, 0) == 0 && snd_mixer_attach(mx2, "hw:0") == 0 && snd_mixer_selem_register(mx2, NULL, NULL) == 0 && snd_mixer_load(mx2) == 0)
	{
		snd_mixer_elem_t* e2 = snd_mixer_find_selem(mx2, sid);
		int a = -1, b = -1;
		if (e2)
		{
			snd_mixer_selem_get_playback_switch(e2, SND_MIXER_SCHN_FRONT_LEFT, &a);
			snd_mixer_selem_get_playback_switch(e2, SND_MIXER_SCHN_FRONT_RIGHT, &b);
		}
		printf("a fresh handle reads: found=%d left=%d right=%d\n", e2 != NULL, a, b);
	}
	snd_mixer_close(mx);
	return 0;
}
