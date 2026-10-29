#pragma once
// banana-ui: the theme's menu sounds (rnds/sounds/*.wav) through ALSA, and an optional ambient background loop. The
// device is opened on the first sound and then kept (the loop between cues, silence else): a cue is instant because
// there is no DSP graph to instantiate, which is the whole point on this card. It is closed while the screen is off
// (setScreenOn(false), from the burn-in guard's blank -- the real idle) and at once when a game starts (release()),
// so nothing holds the audio device when nothing needs it.
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
		// The burn-in guard's hook (idle.h): the stream lives while the screen is on. off: the device is released
		// (the screen being off *is* the idle); on: it is pre-opened, so the next cue needs no DSP graph to be
		// instantiated. The press that wakes the screen is swallowed by the guard, which is free head-start time.
		void setScreenOn(bool on);
		// Open the device now and hold it until the first cue. The boot animation plays from the first frame while
		// the AudioReach card is not openable yet (snd_pcm_open fails, the route reads off,off): without this the
		// first cue opens the device cold, fails, and is lost. Independent of the background loop, which may be off.
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
		std::atomic<bool> screenOff{false};	// the guard blanked the panel: let the device go (see setScreenOn)
		std::atomic<bool> handover{false};	// the last close was release(): the next open follows a game's handover
		std::atomic<bool> devOpen{false};	// the playback device is open (see release())
		std::atomic<bool> bgmOn{false};		// the setting (and 0 while a game or a sleep holds the device)
		std::atomic<bool> warmUp{false};	// the boot warm-up: open the device and hold it until the first cue
		std::atomic<int> bgmGain{280};		// the loop's mix, thousandths of full scale
		std::chrono::steady_clock::time_point bgmStartAt;	// the loop waits until this before opening (the amp needs a moment)
		void run();
	};
}
