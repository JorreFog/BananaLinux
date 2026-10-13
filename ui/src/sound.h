#pragma once
// banana-ui: the theme's menu sounds (rnds/sounds/*.wav) through ALSA. The device is opened on the first sound, kept
// playing (silence between sounds, so the RG DS Plus's amplifiers stay started) and closed after 20 quiet seconds, and
// at once when a game starts (release()), so nothing holds the audio device while a game wants it.
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
		explicit Sound(const std::string& dir);
		~Sound();
		void play(const std::string& name);
		void release();		// close the device now (a game is starting)
		bool enabled = true;

	private:
		struct Clip { std::vector<int16_t> pcm; int rate = 44100, channels = 2; };
		std::map<std::string, Clip> clips;
		std::thread th;
		std::mutex m;
		std::condition_variable cv;
		std::string queued;
		bool stop = false, closeNow = false;
		void run();
	};
}
