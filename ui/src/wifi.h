#pragma once
// banana-ui: Wi-Fi for the settings screen. The networks around (wpa_cli's scan_results through the wifi script),
// the state of the connection (wpa_cli status), and banana/wifi.conf, the wpa_supplicant network blocks the
// settings screen adds to (a block per network, the one chosen last first) and that can be written on a computer.
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace banana
{
	namespace wifi
	{
		struct Network
		{
			std::string ssid;
			int dbm = -100;
			bool secured = false;		// WPA/WPA2/WPA3 personal: a password
			bool saeOnly = false;		// WPA3 only (no WPA2 to fall back on)
			bool unsupported = false;	// enterprise (802.1X) or WEP: wifi.conf by hand
			bool saved = false;			// in wifi.conf already
			bool current = false;		// the one connected now
			int bars() const { return dbm >= -55 ? 3 : dbm >= -67 ? 2 : dbm >= -78 ? 1 : 0; }
		};

		// wpa_cli scan_results: one per name (the strongest), hidden ones left out, strongest first
		std::vector<Network> parseScan(const std::string& results);
		// wpa_cli's escaped SSIDs (\xNN, \\, \") back to bytes
		std::string unescape(const std::string& s);

		struct Status { bool connected = false; std::string state, ssid, ip; };
		Status parseStatus(const std::string& status);

		// the networks in a wifi.conf
		std::vector<std::string> savedNames(const std::string& conf);
		// a network's block written first in wifi.conf (any older block for the same name replaced), with the highest
		// priority so wpa_supplicant tries it first; password empty for an open network
		bool remember(const std::string& conf, const Network& n, const std::string& password);
		bool forget(const std::string& conf, const std::string& ssid);
		// a password wpa_supplicant takes: 8 to 63 printable ASCII characters
		bool passwordOk(const std::string& p);

		// a command run in the background (scans take seconds); sync runs it at once (the headless test)
		class Job
		{
		public:
			~Job();
			void start(const std::string& cmd, bool sync = false);
			bool running() const { return active && !done; }
			// the output once the command has finished (once)
			bool take(std::string& out);
		private:
			std::thread th;
			std::atomic<bool> done{ false };
			bool active = false;
			std::mutex m;
			std::string result;
		};
	}
}
