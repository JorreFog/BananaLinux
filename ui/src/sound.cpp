#include "sound.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#ifdef BANANA_ALSA
#include <alsa/asoundlib.h>
#endif

namespace banana
{
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

	Sound::Sound(const std::string& dir)
	{
		for (const char* n : { "scroll", "select", "back", "fav", "ready", "insert" })
		{
			Clip c;
			if (loadWav(dir + "/" + n + ".wav", c.pcm, c.rate, c.channels)) clips[n] = std::move(c);
		}
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

	void Sound::release()
	{
		{
			std::lock_guard<std::mutex> l(m);
			closeNow = true;
			queued.clear();
		}
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
		snd_pcm_t* pcm = nullptr;
		int openRate = 0, openCh = 0;
		auto closeDev = [&] { if (pcm) { snd_pcm_drain(pcm); snd_pcm_close(pcm); pcm = nullptr; } };
		for (;;)
		{
			std::string name;
			{
				std::unique_lock<std::mutex> l(m);
				bool woke = cv.wait_for(l, std::chrono::seconds(3), [this] { return stop || closeNow || !queued.empty(); });
				if (stop) break;
				if (closeNow || !woke) { closeDev(); closeNow = false; continue; }	// asked to, or quiet for 3 s
				name = queued;
				queued.clear();
			}
			const Clip& c = clips[name];
			if (pcm && (openRate != c.rate || openCh != c.channels)) closeDev();
			if (!pcm)
			{
				if (snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) { pcm = nullptr; continue; }
				if (snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, c.channels, c.rate, 1, 30000) < 0)
				{
					snd_pcm_close(pcm); pcm = nullptr; continue;
				}
				openRate = c.rate; openCh = c.channels;
			}
			// a new sound cuts the one playing (a held direction scrolls fast)
			snd_pcm_drop(pcm);
			snd_pcm_prepare(pcm);
			snd_pcm_sframes_t frames = (snd_pcm_sframes_t)(c.pcm.size() / c.channels);
			const int16_t* p = c.pcm.data();
			while (frames > 0)
			{
				snd_pcm_sframes_t w = snd_pcm_writei(pcm, p, (snd_pcm_uframes_t)frames);
				if (w < 0) { if (snd_pcm_recover(pcm, (int)w, 1) < 0) break; continue; }
				frames -= w; p += w * c.channels;
			}
		}
		closeDev();
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
