#include "sound.h"
#include <unistd.h>
#include "../rnds/RndsUI.h"
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <fstream>
#include <string>
#ifdef BANANA_ALSA
#include <alsa/asoundlib.h>
#include <sys/stat.h>
#endif

namespace banana
{
// Are the RPC's DSP frontends all free right now? /proc says so, and "closed"/"disconnected" are the two states in
// which a stream is not holding the graph. The DSP tears a graph down after the close returns, so a route written
// straight after closing a stream can be written to a graph that is on its way out.
static bool dspFrontendsFree()
{
	FILE* p = popen("cat /proc/asound/card0/pcm*p/sub0/status 2>/dev/null", "r");
	if (!p) return true;
	char buf[64];
	bool busy = false;
	while (fgets(buf, sizeof buf, p))
	{
		size_t n = strlen(buf);
		while (n && isspace((unsigned char)buf[n - 1])) buf[--n] = 0;
		if (strcmp(buf, "closed") && strcmp(buf, "disconnected")) busy = true;
	}
	pclose(p);
	return !busy;
}


	// 16-bit PCM WAV only (what rnds/gen_sounds.py writes)
	static bool loadWav(const std::string& path, std::vector<int16_t>& pcm, int& rate, int& channels)
	{
		FILE* f = fopen(path.c_str(), "rb");
		if (!f) return false;
		std::vector<uint8_t> b;
		uint8_t buf[4096];
		size_t n;
		while ((n = fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
		fclose(f);
		if (b.size() < 44 || memcmp(b.data(), "RIFF", 4) || memcmp(b.data() + 8, "WAVE", 4)) return false;
		size_t at = 12;
		int bits = 0;
		while (at + 8 <= b.size())
		{
			uint32_t len = b[at + 4] | (b[at + 5] << 8) | (b[at + 6] << 16) | ((uint32_t)b[at + 7] << 24);
			const uint8_t* c = b.data() + at + 8;
			if (!memcmp(b.data() + at, "fmt ", 4) && len >= 16)
			{
				channels = c[2] | (c[3] << 8);
				rate = c[4] | (c[5] << 8) | (c[6] << 16) | (c[7] << 24);
				bits = c[14] | (c[15] << 8);
			}
			else if (!memcmp(b.data() + at, "data", 4))
			{
				if (bits != 16 || channels < 1) return false;
				len = (uint32_t)std::min<size_t>(len, b.size() - at - 8);
				pcm.resize(len / 2);
				memcpy(pcm.data(), c, pcm.size() * 2);
				return true;
			}
			at += 8 + len + (len & 1);
		}
		return false;
	}

#ifdef BANANA_ALSA
	// A log line prefixed with the seconds since boot, so the sound's opens and cues can be lined up with the kernel's
	// (bananad's and the menad's boot marks, dmesg's amp start failures).
	static void slog(const char* fmt, ...)
	{
		char msg[256];
		va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
		double up = 0; FILE* f = fopen("/proc/uptime", "r");
		if (f) { if (fscanf(f, "%lf", &up) != 1) up = 0; fclose(f); }
		fprintf(stderr, "banana-ui: sound: %6.2f s: %s\n", up, msg);
	}

	// The menu plays on the same device as the games, and the card's output is a 48 kHz part: anything else
	// is resampled there by alsa-lib's linear converter (the image has no speexrate). Convert each clip once, here,
	// so the stream is always 48 kHz stereo and no plug resampler is involved. Mono (and >2-channel) clips are made
	// stereo too, so a cue and the ambient loop can always share one stream.
	static void to48k(std::vector<int16_t>& pcm, int& rate, int& channels)
	{
		if (rate == 48000) return;	// (channels are fixed up by loadWav below)
		if (channels < 1 || pcm.size() / channels < 2) return;
		size_t frames = pcm.size() / channels, n = (size_t)((double)frames * 48000 / rate);
		std::vector<int16_t> out(n * channels);
		for (size_t i = 0; i < n; i++)
		{
			double x = (double)i * rate / 48000;
			size_t j = (size_t)x;
			double f = x - j;
			if (j + 1 >= frames) { j = frames - 2; f = 1; }
			for (int c = 0; c < channels; c++)
				out[i * channels + c] = (int16_t)(pcm[j * channels + c] + (pcm[(j + 1) * channels + c] - pcm[j * channels + c]) * f);
		}
		pcm.swap(out);
		rate = 48000;
	}

	// every clip the menu plays is stereo (the mixer below writes one stream for the device)
	static void toStereo(std::vector<int16_t>& pcm, int& channels)
	{
		if (channels == 2 || channels < 1 || pcm.size() / channels < 1) return;
		size_t frames = pcm.size() / channels;
		std::vector<int16_t> out(frames * 2);
		for (size_t i = 0; i < frames; i++)
			for (int c = 0; c < 2; c++)
				out[i * 2 + c] = pcm[i * channels + (c < channels ? c : 0)];
		pcm.swap(out);
		channels = 2;
	}
#endif

	Sound::Sound(const std::string& dir)
	{
		for (const char* n : { "scroll", "select", "back", "fav", "ready", "insert" })
		{
			// a user theme's own sound for it (rnds::themeFiles; "off": none), else the theme's (one that doesn't
			// load: the theme's)
			auto own = rnds::themeFiles.sounds.find(n);
			if (own != rnds::themeFiles.sounds.end() && own->second == "off") continue;
			Clip c;
			bool got = own != rnds::themeFiles.sounds.end() && loadWav(own->second, c.pcm, c.rate, c.channels);
			if (!got) got = loadWav(dir + "/" + n + ".wav", c.pcm, c.rate, c.channels);
			if (got)
			{
#ifdef BANANA_ALSA
				toStereo(c.pcm, c.channels);
				to48k(c.pcm, c.rate, c.channels);
#endif
				clips[n] = std::move(c);
			}
		}
		// the ambient background loop: a WAV on the card (bgm.wav, or music/bgm.wav), else the image's own default.
		// Written by the user; the settings screen shows "Background Music" only when one is found.
		for (const char* p : { "/data/bgm.wav", "/data/music/bgm.wav", "/usr/share/banana/bgm.wav" })
			if (loadWav(p, bgm.pcm, bgm.rate, bgm.channels)) { bgmLoaded = true; break; }
#ifdef BANANA_ALSA
		if (bgmLoaded) { toStereo(bgm.pcm, bgm.channels); to48k(bgm.pcm, bgm.rate, bgm.channels); }
		bgmStartAt = std::chrono::steady_clock::now();	// setBgm(true) starts the loop from here
#endif
		th = std::thread([this] { run(); });
	}

	Sound::~Sound()
	{
		{
			std::lock_guard<std::mutex> l(m);
			stop = true;
		}
		cv.notify_all();
		if (th.joinable()) th.join();
	}

	void Sound::play(const std::string& name)
	{
		if (!enabled || !clips.count(name)) return;
		{
			std::lock_guard<std::mutex> l(m);
			queued = name;
		}
		cv.notify_all();
	}

	/* The boot warm-up: open the device now, independent of the background loop, and hold it until the first cue.
	 * The splash's insert is ~10 s after the kernel started, when the AudioReach card is not openable yet; the cue's
	 * own open then fails. The runner retries until the card comes up and holds the stream open (silence) ready for
	 * it. Cleared by the first cue, and by a deadline so a splash with no cue can't hold the device forever. */
	void Sound::warm()
	{
		if (!enabled) return;
		warmUp = true;
#ifdef BANANA_ALSA
		slog("warm(): the first cue will find the device open");
#endif
		cv.notify_all();
	}

	/* idle.h's burn-in guard calls this: the stream lives exactly while the screen is on. Blanking the panel is the
	 * real idle, so the device can go then -- and coming back from it pre-opens the stream, so the next cue is
	 * instant. The wake press itself is swallowed by the guard, which is the head-start. */
	void Sound::setScreenOn(bool on)
	{
		if (!enabled) return;
		if (on) { warm(); return; }
		{
			std::lock_guard<std::mutex> l(m);
			screenOff = true;
		}
		cv.notify_all();
	}

	void Sound::setBgm(bool on)	{
		bgmOn = on && bgmLoaded;
		/* The amp's PLL and the DSP graph need a moment after the card comes up (dmesg: start failure (-1)
		 * every few seconds until the amp locks), and the first few seconds of a boot are when the splash
		 * plays. The loop opens the device as soon as the setting turns on -- writes retry in place while the
		 * amp settles (writeFrames), so the device is warm and ready by the time the splash cue arrives -- and
		 * a closed device keeps the stream open for it instead of closing after a quiet spell. */
		if (on && bgmLoaded && !devOpen.load()) bgmStartAt = std::chrono::steady_clock::now();
#ifdef BANANA_ALSA
		slog("setBgm(%d) -> bgmOn=%d (loaded=%d)", on ? 1 : 0, bgmOn.load() ? 1 : 0, bgmLoaded ? 1 : 0);
#endif
		cv.notify_all();	// a closed device has to know there is a loop to open for
	}

	void Sound::setBgmGain(int permille)
	{
		if (permille < 0) permille = 0;
		if (permille > 1000) permille = 1000;
		bgmGain = permille;
	}

	void Sound::release()
	{
		{
			std::lock_guard<std::mutex> l(m);
#ifdef BANANA_ALSA
			slog("release(): the device is handed over");
#endif
			closeNow = true;
			handover = true;	// the game that starts now has the frontend: the menu's next open needs the DSP's settle
			queued.clear();
			bgmOn = false;	// the game takes the device; main.cpp calls setBgm(true) again when it comes back
		}
		/* the DSP gives one stream per frontend: the game that is about to start opens the same one, so wait for the
		 * thread to close it (it does within a frame or two) instead of racing it -- RetroArch's ALSA driver asserts
		 * on a handle it couldn't open */
		for (int i = 0; i < 80 && devOpen; i++) usleep(5000);
		cv.notify_all();
		// wait until the device is closed
		for (int i = 0; i < 100; i++)
		{
			{
				std::lock_guard<std::mutex> l(m);
				if (!closeNow) return;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
	}

	void Sound::run()
	{
#ifdef BANANA_ALSA
		// While the device is open the stream never stops: after a sound, silence follows. The DSP's output is
		// started when the stream powers up, and the first try fails because the I2S clock isn't running yet; a
		// running stream lets the retry find its clock. Writing 5 ms at a time also lets a new sound cut the one
		// playing (a held direction scrolls fast) within the buffer. QUIET is the boot warm-up's deadline: it holds
		// the device open for the first cue, then stops.
		const auto QUIET = std::chrono::seconds(20);
		snd_pcm_t* pcm = nullptr;
		int openRate = 0, openCh = 0;
		std::vector<int16_t> silence, mix;
		auto closeDev = [&](const char* why) { if (pcm) { slog("closing (%s)", why); snd_pcm_drop(pcm); snd_pcm_close(pcm); pcm = nullptr; devOpen = false; } };
		// Where sound plays (speakers or headphones) is bananad's /run/banana/asound.conf, which it replaces when the
		// jack changes. alsa-lib reads its configuration once per process (a file it loaded through a hook, as
		// /etc/asound.conf is, isn't looked at again), so a change means: close, forget the configuration, open again.

		/* The RPC's DSP allows one stream per frontend, so the menu does have to hand its own over before a game
		 * opens the same one -- and that is what Sound::release() is for, which runGame() calls for every launch.
		 * Closing the device shortly after each cue as well (150 ms here, once) cost the handheld a whole graph
		 * instantiation per button press: the DSP had the frontend up only after the cue was over, so a single press
		 * was silent and only a fast run of them -- which kept the device open across the gaps -- was heard. The
		 * stream now stays open between cues as it does on every other board, and the game's claim on it is the
		 * explicit release. */
		const auto quiet = QUIET;
		auto routeId = [] { struct stat st; return stat("/run/banana/asound.conf", &st) ? 0LL : (long long)st.st_ino * 1000003LL + st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec; };
		long long routeLoaded = routeId();
		int routeCheck = 0;
		bool first = true;		// the first cue of the session says what the DSP answered (see below)
		/* The DSP's route mixers through the ALSA mixer API, not `amixer`: this used to fork four or five processes
		 * per open (applyRoute's two, the read-back's popen, the first-cue check), ~20-50 ms each, which on the
		 * menu's own reopen was most of the delay before the first cue. One handle, kept for the thread's life and
		 * dropped when alsa-lib's configuration is (the jack moved). */
		snd_mixer_t* mx = nullptr;
		auto dropMixer = [&] { if (mx) { snd_mixer_close(mx); mx = nullptr; } };
		auto mixer = [&]() -> bool
		{
			if (mx) return true;
			if (snd_mixer_open(&mx, 0) < 0) { mx = nullptr; return false; }
			if (snd_mixer_attach(mx, "hw:0") < 0 || snd_mixer_selem_register(mx, nullptr, nullptr) < 0 || snd_mixer_load(mx) < 0)
			{
				snd_mixer_close(mx);
				mx = nullptr;
				return false;
			}
			return true;
		};
		/* The DSP **rebuilds its controls with each graph's life**, and alsa-lib's mixer keeps the element list it
		 * read: an element found a minute ago points at a control that no longer exists, and the write then goes
		 * nowhere, silently (measured on the unit with tools/route-probe: a long-lived handle found the selem and
		 * "set" it with no effect, a fresh one moved it). So every look-up starts from a whole fresh mixer --
		 * open, attach, register, load, exactly as the probe does; re-registering an already-loaded handle fails,
		 * which is what the first version of this did (the boot log read '?' for the route). A local parse of the
		 * same ~180 controls, no device I/O, no fork. */
		auto refresh = [&] { dropMixer(); return mixer(); };
		auto selem = [&](const std::string& name) -> snd_mixer_elem_t*
		{
			if (!mx) return nullptr;
			snd_mixer_selem_id_t* sid = nullptr;
			snd_mixer_selem_id_alloca(&sid);
			snd_mixer_selem_id_set_index(sid, 0);
			snd_mixer_selem_id_set_name(sid, name.c_str());
			return snd_mixer_find_selem(mx, sid);
		};
		auto switchTo = [&](const std::string& name, int on) -> bool
		{
			snd_mixer_elem_t* e = selem(name);
			if (!e) return false;
			/* these switches are not "joined" (the probe: has_playback_switch 1, joined 0), so both channels are set */
			snd_mixer_selem_set_playback_switch_all(e, on);
			return true;
		};
		/* bananad's published route, written into the DSP's mixer: "control=values" a line, its speakers or its
		 * codec depending on the jack. The fallback (no file: an older card, or a cue before bananad has written
		 * one) is the speakers, which is what every handheld without a DSP routing switch wants. */
		auto applyRoute = [&]
		{
			if (!refresh()) return;
			std::ifstream rt("/run/banana/audio-route");
			std::string line;
			bool any = false;
			while (std::getline(rt, line))
			{
				size_t eq = line.rfind('=');
				if (eq == std::string::npos || eq + 1 >= line.size()) continue;
				if (switchTo(line.substr(0, eq), line.compare(eq + 1, 2, "on") == 0 ? 1 : 0)) any = true;
			}
			if (!any) switchTo("SENARY_MI2S_RX Audio Mixer MultiMedia1", 1);
		};
		/* Still as bananad published it? A graph keeps the route it was instantiated with, so a route taken away
		 * under a live stream cannot be repaired by writing it again -- only by opening a new stream. Reading one
		 * mixer is microseconds now; a cue that would otherwise be heard nowhere is not. */
		auto routeIsAsPublished = [&]
		{
			if (!refresh()) return true;
			std::ifstream rt("/run/banana/audio-route");
			std::string line;
			if (!std::getline(rt, line)) return true;	// nothing published (not the RPC's card): never stale
			size_t eq = line.rfind('=');
			if (eq == std::string::npos) return true;
			std::string name = line.substr(0, eq);
			/* the first channel's answer only: these DSP mixers take the value for the pair and answer with the
			 * second one forced off ("on,on" reads back "on,off"), so comparing every value would call a perfectly
			 * good route stale and have the menu rebuild its stream on every cue */
			snd_mixer_elem_t* e = selem(name);
			if (!e) return true;	// (not up yet, or not this card: don't hold the open up)
			int v = 0;
			if (snd_mixer_selem_get_playback_switch(e, SND_MIXER_SCHN_FRONT_LEFT, &v) < 0) return true;
			return v == (line.compare(eq + 1, 2, "on") == 0 ? 1 : 0);
		};
		/* The RPC's DSP (audioreach): a frontend's graph is instantiated at the open and takes the mixer values it
		 * finds at that moment, and closing a stream tears that graph down -- asynchronously, which is the part that
		 * matters here. So the order that works is: make sure nothing holds the frontend, write the route, open.
		 * Written while a teardown is still running, the value is dropped with the graph it was meant for, and the
		 * stream that opens next takes the topology's own "off" and stays silent for its whole life. run-retroarch
		 * waits exactly this way; the menu now does the same. */
		auto openDev = [&](const Clip& c) -> bool
		{
			slog("opening the device");
			if (long long r = routeId(); r != routeLoaded) { snd_config_update_free_global(); dropMixer(); routeLoaded = r; }
			if (access("/proc/asound/card0", F_OK) == 0)
			{
				/* The DSP latches the mixer values when the frontend's graph is instantiated -- at our open, not
				 * at bananad's -- so the route has to be written before anything opens the frontend. A probe that
				 * opened and closed first instantiated the graph with the mixers still at their boot defaults (the
				 * kernel: "no backend DAIs enabled for MultiMedia1 Playback") and left its teardown racing the real
				 * open, which came up half connected: the first write returned -EIO and prepare -EBUSY, and the
				 * stream was silent until it closed and reopened. run-retroarch writes its route the same way. */
				applyRoute();
				/* The DSP does not take a mixer write until it is ready, and rcS's and bananad's own retries can be
				 * over by then (their logs read "still reads 'off,off' 3 s after rcS gave up"); the menu would open
				 * an unrouted graph, which is instantiated with the topology's own 'off' and stays silent for the
				 * whole session. Make sure the route this stream is about to latch reads back as published before
				 * opening, retrying the write rather than opening on hope. routeIsAsPublished answers true when
				 * nothing is published (not this card), so this is a no-op elsewhere. */
				for (int t = 0; t < 75 && !routeIsAsPublished(); t++)
				{
					applyRoute();
					std::this_thread::sleep_for(std::chrono::milliseconds(200));
				}
				/* The frontend poll and the settle are for a *handover*: the game that just ended may still be
				 * holding the DSP frontend, and an open inside its teardown comes up half connected (the amps answer
				 * "start failure (-1)": measured on the RPC with a game and a menu cue overlapping by a few hundred
				 * ms). The menu's *own* reopen -- its own graph just went down -- needs neither, and they were most
				 * of the delay before its first cue. release() raises `handover` for the open that follows a game. */
				if (handover.exchange(false))
				{
					bool wasHeld = false;
					for (int t = 0; t < 12 && !dspFrontendsFree(); t++)
					{
						wasHeld = true;
						std::this_thread::sleep_for(std::chrono::milliseconds(100));
					}
					slog("DSP frontends %s", wasHeld ? "were held: waited" : "were free");
					if (wasHeld)
						std::this_thread::sleep_for(std::chrono::milliseconds(600));
				}
				if (first)	// one line, so a silent menu says what the DSP was told and what it answered
				{
					refresh();
					snd_mixer_elem_t* e = selem("SENARY_MI2S_RX Audio Mixer MultiMedia1");
					int v = -1;
					if (e) snd_mixer_selem_get_playback_switch(e, SND_MIXER_SCHN_FRONT_LEFT, &v);
					fprintf(stderr, "banana-ui: sound: the speaker route reads '%s' before the first cue\n",
						v < 0 ? "?" : v ? "on" : "off");
					first = false;
				}
			}
			int e = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
			if (e < 0) { slog("open failed: %s", snd_strerror(e)); pcm = nullptr; return false; }
			/* The host buffer: as small as the DSP will take, so a cue starts as soon as the mixer has it. A 128 ms
			 * buffer is refused outright on this card (no sound at all, docs/RPC-PORT.md), so try 15 ms and 20 ms
			 * before the 30 ms that has always worked; what took is logged. */
			int us = 0;
			for (int tryUs : { 15000, 20000, 30000 })
				if (snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, c.channels, c.rate, 1, (unsigned)tryUs) >= 0)
				{ us = tryUs; break; }
			if (!us)
			{
				slog("set_params failed"); snd_pcm_close(pcm); pcm = nullptr; return false;
			}
			openRate = c.rate; openCh = c.channels;
			devOpen = true;
			slog("device open, %d Hz, %d ch, %d ms buffer", openRate, openCh, us / 1000);
			silence.assign((size_t)(openRate / 200) * openCh, 0);
			return true;
		};
		auto writeFrames = [&](const int16_t* p, size_t frames) -> bool
		{
			int giveUp = 0;
			while (frames > 0)
			{
				snd_pcm_sframes_t w = snd_pcm_writei(pcm, p, (snd_pcm_uframes_t)frames);	// blocks while the buffer is full
				if (w < 0)
				{
					/* The AW88166's PLL and the DSP graph need a moment after the card comes up (dmesg: start
					 * failure (-1) every few seconds until the amp locks), and every failed write is reported
					 * here. Closing the device on one tears the DSP's graph down and rebuilds it, which is the
					 * choppy sound: the graph is gone for as long as the amp is failing. Prepare the stream in
					 * place and keep feeding it silence until it takes; the write succeeds once the PLL locks. */
					if (++giveUp > 20) { slog("write failed repeatedly (%s); giving up", snd_strerror((int)w)); return false; }
					slog("write failed (%s)", snd_strerror((int)w));
					int r = snd_pcm_recover(pcm, (int)w, 1);
					if (r < 0)
					{
						/* During boot, the Qualcomm AudioReach DSP and AW88166 amp hardware take ~1-2 seconds
						 * to finish DAPM power-up. An initial write during this window receives -EIO and
						 * recover/prepare returns -EBUSY. Do NOT close the device (which destroys the DSP graph
						 * and forces another multi-second teardown/rebuild cycle); sleep in short increments
						 * and retry snd_pcm_prepare() until the hardware finishes powering up. */
						for (int retry = 0; retry < 25 && r < 0; retry++)
						{
							std::this_thread::sleep_for(std::chrono::milliseconds(100));
							r = snd_pcm_prepare(pcm);
							// every 5th only: at boot the amps are not up for ~10 s and this retries the whole time,
							// which was a wall of one line per 100 ms in the log (25 lines per attempt)
							if (r < 0 && retry % 5 == 4) slog("prepare retry %d/25 (%s)", retry + 1, snd_strerror(r));
						}
					}
					if (r < 0) { slog("recover/prepare failed (%s); giving up", snd_strerror(r)); return false; }
					for (int t = 0; t < 80 && snd_pcm_writei(pcm, silence.data(), (snd_pcm_uframes_t)(silence.size() / openCh)) < 0; t++)
						std::this_thread::sleep_for(std::chrono::milliseconds(50));
					continue;
				}
				giveUp = 0;
				frames -= (size_t)w; p += (size_t)w * openCh;
			}
			return true;
		};
		const Clip* cur = nullptr;
		size_t at = 0;			// cur's next frame
		size_t bgmAt = 0;		// the loop's next frame
		auto lastSound = std::chrono::steady_clock::now();
		for (;;)
		{
			std::string name;
			{
				std::unique_lock<std::mutex> l(m);
				// closed: nothing to do until a cue, a game's release, or the loop wanting the device again
				if (!pcm) cv.wait(l, [this] { return stop || closeNow || screenOff.load() || !queued.empty() || bgmOn.load() || warmUp.load(); });
				if (stop) break;
				if (closeNow) { closeDev("release"); cur = nullptr; closeNow = false; continue; }	// a game is starting
				// the burn-in guard blanked the screen (idle.h): the real idle, so let the device go -- unless a
				// cue came with it, in which case the screen is being used again
				if (screenOff.exchange(false) && queued.empty())
				{
					closeDev("screen off");
					cur = nullptr;
					continue;
				}
				name = queued;
				queued.clear();
			}
			auto now = std::chrono::steady_clock::now();
			// The loop opens the device as soon as the setting turns on (bgmStartAt is set in the constructor and
			// not pushed forward, so it is already past) and keeps it open; a closed device stays open for it
			// instead of closing after a quiet spell. The amp settles while writeFrames retries in place.
			const bool wantBgm = bgmOn.load() && bgmLoaded && (devOpen.load() || now >= bgmStartAt);
			// the boot warm-up opens and holds the device the same way, with no loop to play (the splash's cue)
			const bool wantOpen = wantBgm || warmUp.load();
			if (!name.empty())
			{
				static bool loggedCue = false;
				if (!loggedCue || name == "insert") { slog("cue %s", name.c_str()); loggedCue = true; }
				const Clip& c = clips[name];
				/* bananad republishes the route (it writes /run/banana/audio-route and rewrites /run/banana/asound.conf
				   together) only on a jack change, so while asound.conf is the one this stream was opened with the DSP
				   still holds the route applyRoute wrote then: there is nothing to check, and the amixer fork the
				   check makes has no business on every cue (it is the cue's own start that is delayed). When the route
				   did move, the check runs as before. */
				if (pcm && routeId() != routeLoaded && !routeIsAsPublished())
				{
					/* write it back, and only if the DSP will not take it that way (a graph keeps the route it was
					   instantiated with) rebuild the stream -- one extra mixer read, on the one cue that was wrong */
					applyRoute();
					if (!routeIsAsPublished()) closeDev("route stale");
				}
				if (pcm && (openRate != c.rate || openCh != c.channels)) closeDev("format");
				if (!pcm && !openDev(c))
				{
					// the card is not up yet (a boot-time cue): keep the cue and try again, so the splash's click
					// plays once it is, instead of being lost to the failed open
					if (warmUp.load())
					{
						std::lock_guard<std::mutex> l(m);
						if (queued.empty()) queued = name;
						std::this_thread::sleep_for(std::chrono::milliseconds(250));
					}
					continue;
				}
				cur = &c;
				at = 0;
				lastSound = now;
				warmUp = false;	// the device is open and the cue is playing: the boot warm-up is done
			}
			else if (!cur)
			{
				// Nothing to play: the device stays open (a cue is then instant). Only the warm-up has a deadline,
				// so a splash with no cue cannot hold it forever, and the guard's blank and a game's release are
				// what else let it go.
				if (warmUp.load() && now - lastSound >= quiet) warmUp = false;	// the cue never came: stop holding the device
				if (!pcm && wantOpen && !openDev(warmUp.load() ? warmClip : bgm))
				{
					if (warmUp.load()) std::this_thread::sleep_for(std::chrono::milliseconds(250));	// the card isn't up yet
					continue;
				}
			}
			if (!pcm) continue;
			// the jack changed while the stream runs (every 250 ms): the next sound opens where sound plays now
			if (++routeCheck >= 25) { routeCheck = 0; if (routeId() != routeLoaded) { closeDev("route changed"); cur = nullptr; continue; } }

			const bool haveBgm = wantBgm && bgmLoaded && bgmGain.load() > 0;
			size_t nframes = (size_t)(openRate / 200);	// the next 5 ms: the cue, the loop, or silence
			if (!cur && !haveBgm)
			{
				if (!writeFrames(silence.data(), silence.size() / openCh)) { closeDev("write err (silence)"); continue; }
			}
			else
			{
				// the cue at full level, the ambient loop (a few percent) under it: one stream, mixed here
				if (mix.size() < nframes * openCh) mix.resize(nframes * openCh);
				std::fill(mix.begin(), mix.begin() + nframes * openCh, 0);
				if (cur)
				{
					size_t total = cur->pcm.size() / openCh;
					size_t take = std::min(nframes, total - at);
					const int16_t* s = cur->pcm.data() + at * openCh;
					std::copy(s, s + take * openCh, mix.begin());
					at += take;
					if (at >= total) { cur = nullptr; lastSound = now; }
				}
				if (haveBgm && !bgm.pcm.empty())
				{
					size_t total = bgm.pcm.size() / openCh;
					int g = bgmGain.load();
					for (size_t i = 0; i < nframes; i++)
					{
						size_t j = bgmAt + i; if (j >= total) j -= total;	// (bgmAt < total, nframes short)
						for (int c = 0; c < openCh; c++)
						{
							int v = mix[i * openCh + c] + (int)bgm.pcm[j * openCh + c] * g / 1000;
							if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
							mix[i * openCh + c] = (int16_t)v;
						}
					}
					bgmAt = (bgmAt + nframes) % total;
				}
				if (!writeFrames(mix.data(), nframes)) { closeDev("write err (mix)"); cur = nullptr; continue; }
			}
		}
		closeDev("end");
		dropMixer();
#else
		std::unique_lock<std::mutex> l(m);
		for (;;)
		{
			cv.wait(l, [this] { return stop || closeNow || !queued.empty(); });
			if (stop) break;
			closeNow = false;
			queued.clear();
		}
#endif
	}
}
