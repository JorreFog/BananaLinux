#pragma once
// banana-ui on Android: the platform under the menu. On BananaOS's own image the menu asks small shell tools
// (/usr/libexec/banana/volume, backlight, handheld, sysstat, wifi, bluetooth, banana-perf...) and writes a few sysfs
// files (the backlights); none of them exist under Android. The core's ~45 calls to them stay as they are, and
// android_tools.cpp takes each one where it leaves the core (popen, system, exec, fopen of a mapped path) and runs it
// on this interface instead. Its Android implementation (android_platform.cpp) asks the launcher's Java side
// (AudioManager, WifiManager, BluetoothAdapter, Settings panels) and banana-hwd, the ROM's hardware daemon (both
// backlights, the fan, the stick lights, the performance modes, the stats: android/system/hwd/PROTOCOL.md).
#include <string>
#include <vector>

namespace banana
{
	namespace platform
	{
		class Platform
		{
		public:
			virtual ~Platform() = default;
			// the sound's level, 0-100 (-1: unknown)
			virtual int volume() = 0;
			virtual bool setVolume(int percent) = 0;
			// a screen's brightness in thousandths (0 the top, 1 the bottom); -1 unknown
			virtual int brightness(int screen) = 0;
			virtual bool setBrightness(int screen, int permille) = 0;
			// the wifi tool's verbs (up, down, restart, scan, status, test): its output and exit status
			virtual int wifi(const std::vector<std::string>& args, std::string& out) = 0;
			// the bluetooth tool's (up, down, status, devices, scan, pair/connect/disconnect/forget <MAC>, audio)
			virtual int bluetooth(const std::vector<std::string>& args, std::string& out) = 0;
			// the handheld tool's (caps, fan, leds, leds-effect, leds-system, gpu, boot, sleep)
			virtual int handheld(const std::vector<std::string>& args, std::string& out) = 0;
			// sysstat's lines (name=value: cpu, cpu_mhz, cpu_c, gpu, gpu_mhz, gpu_c, mem_*, power_*, battery, fan_pct)
			virtual std::string sysstat() = 0;
			// banana-perf's modes: ui, powersave, balanced, steady, performance, superdrastic
			virtual bool performance(const std::string& mode) = 0;
			// Settings > System > Time zone (a POSIX TZ rule, as settings.conf keeps it)
			virtual bool setTimeZone(const std::string& tz) = 0;
		};

		Platform& get();

		// a tool's command line as the core wrote it (argv[0] the tool's path or name): run on the Platform, its output
		// in out, its exit status returned (127: a tool the Android build doesn't have, as sh says for a missing one)
		int runTool(const std::vector<std::string>& argv, const std::string& in, std::string& out);
		// a tool's name, when path is one of the shell tools this layer stands in for ("" otherwise)
		std::string toolName(const std::string& path);
	}
}
