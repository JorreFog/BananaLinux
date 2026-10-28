#pragma once
// banana-ui: the theme's menu sounds (rnds/sounds/*.wav) through ALSA, and an optional ambient background loop. The
// device is opened on the first sound, kept playing (the loop between cues, silence else), and closed after 20 quiet
// seconds with no loop, and at once when a game starts (release()), so nothing holds the audio device while a game
// wants it.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace banana
{
	class Sound
	{
	public:
		// dir: the theme's sounds/. The ambient loop is looked for on the card and in the image (bgmCandidatePaths).
		explicit Sound(const std::string& dir);
		~Sound();
		void play(const std::string& name);
		// Open the device now and hold it until the first cue. The boot splash's insert is the first cue, ~1 s into
		// the animation and only ~10 s after the kernel started, when the AudioReach card is not openable yet
		// (snd_pcm_open fails, the route reads off,off): without this the cue opens the device cold, fails, and is
		// lost. Independent of the background loop, which may be off.
		void warm();
		void release();		// close the device now, and wait for it: a game opens the same frontend next
		// The ambient background loop: on plays it between cues (the "Background Music" setting); gain is its mix
		// level in thousandths (0: silent but the device stays open, e.g. the burn-in guard's blank stage)
		void setBgm(bool on);
		void setBgmGain(int permille);
		bool hasBgm() const { return bgmLoaded; }
		bool enabled = true;

	private:
		struct Clip { std::vector<int16_t> pcm; int rate = 44100, channels = 2; };
		std::map<std::string, Clip> clips;
		Clip bgm;
		bool bgmLoaded = false;
		Clip warmClip{ {}, 48000, 2 };	// the format the menu's cues are converted to (openDev needs a Clip to open with)
		std::thread th;
		std::mutex m;
		std::condition_variable cv;
		std::string queued;
		bool stop = false, closeNow = false;
		std::atomic<bool> devOpen{false};	// the playback device is open (see release())
		std::atomic<bool> bgmOn{false};		// the setting (and 0 while a game or a sleep holds the device)
		std::atomic<bool> warmUp{false};	// the boot warm-up: open the device and hold it until the first cue
		std::atomic<int> bgmGain{280};		// the loop's mix, thousandths of full scale
		std::chrono::steady_clock::time_point bgmStartAt;	// the loop waits until this before opening (the amp needs a moment)
		void run();
	};
}
