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
		// While the device is open the stream never stops: after a sound, silence follows, until QUIET with nothing to
		// play. The RG DS Plus's speaker amplifiers (aw88166) are started by their driver when the stream powers up,
		// and the first try fails because the I2S clock isn't running yet; the next try is ~0.4 s later and finds no
		// clock either if a short click has already ended, so an amplifier gave up for the whole time the device was
		// open (one speaker, or none). A running stream lets the retry find its clock. Writing 10 ms at a time also
		// lets a new sound cut the one playing (a held direction scrolls fast) within the 30 ms buffer.
		const auto QUIET = std::chrono::seconds(20);
		snd_pcm_t* pcm = nullptr;
		int openRate = 0, openCh = 0;
		std::vector<int16_t> silence;
		auto closeDev = [&] { if (pcm) { snd_pcm_drop(pcm); snd_pcm_close(pcm); pcm = nullptr; } };
		const Clip* cur = nullptr;
		size_t at = 0;			// cur's next frame
		auto lastSound = std::chrono::steady_clock::now();
		for (;;)
		{
			std::string name;
			{
				std::unique_lock<std::mutex> l(m);
				if (!pcm) cv.wait(l, [this] { return stop || closeNow || !queued.empty(); });	// closed: nothing to do
				if (stop) break;
				if (closeNow) { closeDev(); cur = nullptr; closeNow = false; continue; }	// a game is starting
				name = queued;
				queued.clear();
			}
			auto now = std::chrono::steady_clock::now();
			if (!name.empty())
			{
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
					silence.assign((size_t)(openRate / 100) * openCh, 0);
				}
				cur = &c;
				at = 0;
				lastSound = now;
			}
			else if (!cur && now - lastSound >= QUIET) { closeDev(); continue; }
			// the next 10 ms: the sound's, or silence
			const int16_t* p = silence.data();
			snd_pcm_sframes_t frames = (snd_pcm_sframes_t)(silence.size() / openCh);
			if (cur)
			{
				size_t total = cur->pcm.size() / openCh;
				frames = (snd_pcm_sframes_t)std::min<size_t>((size_t)frames, total - at);
				p = cur->pcm.data() + at * openCh;
				at += (size_t)frames;
				if (at >= total) { cur = nullptr; lastSound = now; }
			}
			while (frames > 0)
			{
				snd_pcm_sframes_t w = snd_pcm_writei(pcm, p, (snd_pcm_uframes_t)frames);	// blocks while the buffer is full
				if (w < 0) { if (snd_pcm_recover(pcm, (int)w, 1) < 0) { closeDev(); cur = nullptr; break; } continue; }
				frames -= w; p += w * openCh;
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
