#pragma once
// banana-ui: Bluetooth for the settings screen (Settings > Network > Bluetooth devices). The devices come from the
// bluetooth script (usr/libexec/banana/bluetooth devices | scan), one line each:
//   <MAC> \t <paired 0|1> \t <connected 0|1> \t <kind: audio, gamepad, keyboard, mouse, other> \t <name>
// and pair / connect / disconnect / forget answer "OK" or "ERROR <why>" on their last line. The commands run in the
// background with wifi::Job.
#include <string>
#include <vector>

namespace banana
{
	namespace bt
	{
		struct Device
		{
			std::string mac, name, kind;
			bool paired = false, connected = false;
			bool audio() const { return kind == "audio"; }
		};

		// the script's lines: connected first, then paired, then the rest, each in name order; malformed lines left out
		std::vector<Device> parseDevices(const std::string& out);
		// a command's answer: true for OK; why the reason after ERROR ("" when it gave none)
		bool parseResult(const std::string& out, std::string& why);
		// what a device is, for its row: "Headphones", "Controller", "Keyboard", "Mouse", ""
		std::string kindName(const std::string& kind);
	}
}
