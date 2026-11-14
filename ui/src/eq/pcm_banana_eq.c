/* banana_eq: the speaker EQ, an ALSA filter plugin (alsa-lib's extplug) that bananad puts between `default`'s plug
 * and the speaker card while sound plays on the speakers (never the headphones or Bluetooth):
 *
 *   pcm.!default { type asym playback.pcm { type plug slave { pcm { type banana_eq slave.pcm "hw:1" }
 *                  format S16_LE channels 2 rate 48000 } } ... }
 *
 * It filters inside each program's own audio thread, as plug's conversions do: no sound server, no extra copy, no
 * latency. The profile (Equalizer APO's parametric text, see eq.h) is the file bananad names, /run/banana/speaker-eq
 * unless `file` says otherwise. It is looked at again every 10 ms of sound (one stat), so a change in Settings is heard
 * at once in a game that is already playing, and the menu's beep for the press that made it (written after the file)
 * is the new profile's from its start, not 0.1 s of the old one's (up to 13 dB apart: the beeps are where a speaker's
 * profile cuts); no file, an empty one or one with no filters: the samples pass through untouched.
 *
 * Built as libasound_module_pcm_banana_eq.so in alsa-lib's plugin folder, where `type banana_eq` finds it.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include "eq.h"

#define EQ_DEFAULT_FILE "/run/banana/speaker-eq"

typedef struct {
	snd_pcm_extplug_t ext;
	char path[256];
	struct stat seen;		/* the file as it was last read (st_ino 0: not there) */
	unsigned since;			/* frames since the file was last looked at */
	struct eq_profile prof;
	struct eq_chain chain;
	unsigned channels;
	float *z;			/* each channel's filter state: channels x EQ_MAX_BANDS x 2 */
} banana_eq_t;

/* the profile again if the file changed (or came or went); each filter still in it keeps its state (eq_carry), so a
 * change mid-song is a change of tone, not a click */
static void eq_reload(banana_eq_t *eq, int force)
{
	struct stat st;
	if (stat(eq->path, &st) < 0) memset(&st, 0, sizeof st);
	if (!force && st.st_ino == eq->seen.st_ino && st.st_size == eq->seen.st_size
	    && st.st_mtim.tv_sec == eq->seen.st_mtim.tv_sec && st.st_mtim.tv_nsec == eq->seen.st_mtim.tv_nsec)
		return;
	eq->seen = st;
	char text[8192];
	ssize_t n = 0;
	int fd = st.st_ino ? open(eq->path, O_RDONLY | O_CLOEXEC) : -1;
	if (fd >= 0) {
		n = read(fd, text, sizeof text);
		close(fd);
	}
	eq_parse(text, n > 0 ? (size_t)n : 0, &eq->prof);
	struct eq_chain ch;
	eq_design(&eq->prof, eq->ext.rate ? eq->ext.rate : 48000, &ch);
	for (unsigned c = 0; eq->z && c < eq->channels; c++) eq_carry(&eq->chain, &ch, eq->z + c * EQ_MAX_BANDS * 2);
	eq->chain = ch;
}

static snd_pcm_sframes_t eq_transfer(snd_pcm_extplug_t *ext, const snd_pcm_channel_area_t *dst, snd_pcm_uframes_t dst_off,
				     const snd_pcm_channel_area_t *src, snd_pcm_uframes_t src_off, snd_pcm_uframes_t size)
{
	banana_eq_t *eq = ext->private_data;
	eq->since += size;
	if (eq->since >= ext->rate / 100) {
		eq->since = 0;
		eq_reload(eq, 0);
	}
	if (!eq_active(&eq->chain) || !eq->z) {
		snd_pcm_areas_copy(dst, dst_off, src, src_off, ext->channels, size, SND_PCM_FORMAT_S16);
		return size;
	}
	for (unsigned c = 0; c < ext->channels; c++) {
		/* areas in bits: first, then step per frame (32 for interleaved S16 stereo) */
		const short *in = (const short *)((const char *)src[c].addr + (src[c].first + src_off * src[c].step) / 8);
		short *out = (short *)((char *)dst[c].addr + (dst[c].first + dst_off * dst[c].step) / 8);
		eq_run(&eq->chain, eq->z + c * EQ_MAX_BANDS * 2, in, src[c].step / 16, out, dst[c].step / 16, size);
	}
	return size;
}

/* hw_params are set: the rate and the channels are known; the filters are designed for them, the state starts at rest */
static int eq_init(snd_pcm_extplug_t *ext)
{
	banana_eq_t *eq = ext->private_data;
	if (eq->channels != ext->channels || !eq->z) {
		free(eq->z);
		eq->channels = ext->channels;
		eq->z = calloc((size_t)eq->channels * EQ_MAX_BANDS * 2, sizeof *eq->z);
		if (!eq->z) return -ENOMEM;
	} else
		memset(eq->z, 0, (size_t)eq->channels * EQ_MAX_BANDS * 2 * sizeof *eq->z);
	eq->since = 0;
	eq_reload(eq, 1);
	return 0;
}

static int eq_close(snd_pcm_extplug_t *ext)
{
	banana_eq_t *eq = ext->private_data;
	free(eq->z);
	free(eq);
	return 0;
}

static const snd_pcm_extplug_callback_t eq_callback = {
	.transfer = eq_transfer,
	.init = eq_init,
	.close = eq_close,
};

SND_PCM_PLUGIN_DEFINE_FUNC(banana_eq)
{
	snd_config_iterator_t i, next;
	snd_config_t *slave = NULL;
	const char *file = EQ_DEFAULT_FILE;
	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;
		if (snd_config_get_id(n, &id) < 0) continue;
		if (!strcmp(id, "comment") || !strcmp(id, "type") || !strcmp(id, "hint")) continue;
		if (!strcmp(id, "slave")) { slave = n; continue; }
		if (!strcmp(id, "file")) {
			if (snd_config_get_string(n, &file) < 0) { SNDERR("banana_eq: file must be a string"); return -EINVAL; }
			continue;
		}
		SNDERR("banana_eq: unknown field %s", id);
		return -EINVAL;
	}
	if (!slave) { SNDERR("banana_eq: no slave"); return -EINVAL; }
	if (stream != SND_PCM_STREAM_PLAYBACK) { SNDERR("banana_eq: playback only"); return -EINVAL; }
	banana_eq_t *eq = calloc(1, sizeof *eq);
	if (!eq) return -ENOMEM;
	snprintf(eq->path, sizeof eq->path, "%s", file);
	eq->ext.version = SND_PCM_EXTPLUG_VERSION;
	eq->ext.name = "Banana speaker EQ";
	eq->ext.callback = &eq_callback;
	eq->ext.private_data = eq;
	int err = snd_pcm_extplug_create(&eq->ext, name, root, slave, stream, mode);
	if (err < 0) {
		free(eq);
		return err;
	}
	/* S16 both sides, what the speaker card plays: plug, above it, converts whatever the program sends */
	snd_pcm_extplug_set_param(&eq->ext, SND_PCM_EXTPLUG_HW_FORMAT, SND_PCM_FORMAT_S16);
	snd_pcm_extplug_set_slave_param(&eq->ext, SND_PCM_EXTPLUG_HW_FORMAT, SND_PCM_FORMAT_S16);
	*pcmp = eq->ext.pcm;
	return 0;
}

SND_PCM_PLUGIN_SYMBOL(banana_eq);
