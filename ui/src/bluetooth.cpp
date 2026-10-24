#include "bluetooth.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace banana
{
	namespace bt
	{
		static bool macOk(const std::string& m)
		{
			if (m.size() != 17) return false;
			for (size_t i = 0; i < 17; i++)
				if (i % 3 == 2 ? m[i] != ':' : !isxdigit((unsigned char)m[i])) return false;
			return true;
		}

		std::vector<Device> parseDevices(const std::string& out)
		{
			std::vector<Device> r;
			std::istringstream in(out);
			for (std::string ln; std::getline(in, ln);)
			{
				if (!ln.empty() && ln.back() == '\r') ln.pop_back();
				std::vector<std::string> f;
				size_t a = 0;
				for (int i = 0; i < 4; i++)
				{
					size_t t = ln.find('\t', a);
					if (t == std::string::npos) break;
					f.push_back(ln.substr(a, t - a));
					a = t + 1;
				}
				if (f.size() < 4 || !macOk(f[0])) continue;
				Device d;
				d.mac = f[0];
				d.paired = f[1] == "1";
				d.connected = f[2] == "1";
				d.kind = f[3];
				d.name = ln.substr(a);
				if (d.name.empty()) d.name = d.mac;
				// one line per device (a device listed twice: the last line)
				auto same = std::find_if(r.begin(), r.end(), [&](const Device& x) { return x.mac == d.mac; });
				if (same != r.end()) *same = d; else r.push_back(d);
			}
			auto rank = [](const Device& d) { return d.connected ? 0 : d.paired ? 1 : 2; };
			std::stable_sort(r.begin(), r.end(), [&](const Device& x, const Device& y) {
				if (rank(x) != rank(y)) return rank(x) < rank(y);
				return x.name < y.name;
			});
			return r;
		}

		bool parseResult(const std::string& out, std::string& why)
		{
			why.clear();
			std::string last;
			std::istringstream in(out);
			for (std::string ln; std::getline(in, ln);)
			{
				while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.pop_back();
				if (!ln.empty()) last = ln;
			}
			if (last == "OK") return true;
			if (last.compare(0, 6, "ERROR ") == 0) why = last.substr(6);
			return false;
		}

		std::string kindName(const std::string& kind)
		{
			if (kind == "audio") return "Headphones";
			if (kind == "gamepad") return "Controller";
			if (kind == "keyboard") return "Keyboard";
			if (kind == "mouse") return "Mouse";
			return "";
		}
	}
}
