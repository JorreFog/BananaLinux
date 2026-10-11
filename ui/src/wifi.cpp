#include "wifi.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <unistd.h>

namespace banana
{
	namespace wifi
	{
		static std::string trim(const std::string& s)
		{
			size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
			return a == std::string::npos ? "" : s.substr(a, b - a + 1);
		}

		std::string unescape(const std::string& s)
		{
			std::string r;
			for (size_t i = 0; i < s.size(); i++)
			{
				if (s[i] != '\\' || i + 1 >= s.size()) { r += s[i]; continue; }
				char c = s[++i];
				if (c == 'x' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2]))
				{
					r += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
					i += 2;
				}
				else if (c == 'n') r += '\n';
				else if (c == 't') r += '\t';
				else if (c == 'e') r += '\033';
				else r += c;	// \\ and \"
			}
			return r;
		}

		std::vector<Network> parseScan(const std::string& results)
		{
			std::map<std::string, Network> best;
			std::istringstream in(results);
			std::string ln;
			while (std::getline(in, ln))
			{
				if (!ln.empty() && ln.back() == '\r') ln.pop_back();
				// bssid \t frequency \t signal \t flags \t ssid
				std::vector<std::string> f;
				size_t a = 0;
				for (int i = 0; i < 4; i++)
				{
					size_t t = ln.find('\t', a);
					if (t == std::string::npos) break;
					f.push_back(ln.substr(a, t - a));
					a = t + 1;
				}
				if (f.size() < 4 || f[0] == "bssid / frequency / signal level / flags / ssid") continue;
				Network n;
				n.ssid = unescape(ln.substr(a));
				if (n.ssid.empty() || n.ssid.find('\0') != std::string::npos) continue;	// hidden
				n.dbm = atoi(f[2].c_str());
				const std::string& fl = f[3];
				bool psk = fl.find("PSK") != std::string::npos, sae = fl.find("SAE") != std::string::npos;
				if (fl.find("EAP") != std::string::npos || fl.find("WEP") != std::string::npos || fl.find("OWE") != std::string::npos)
					n.unsupported = true;
				else if (psk || sae) { n.secured = true; n.saeOnly = sae && !psk; }
				auto it = best.find(n.ssid);
				if (it == best.end() || it->second.dbm < n.dbm) best[n.ssid] = n;
			}
			std::vector<Network> r;
			for (auto& kv : best) r.push_back(kv.second);
			std::stable_sort(r.begin(), r.end(), [](const Network& x, const Network& y) { return x.dbm > y.dbm; });
			return r;
		}

		Status parseStatus(const std::string& status)
		{
			Status s;
			std::istringstream in(status);
			std::string ln;
			while (std::getline(in, ln))
			{
				size_t eq = ln.find('=');
				if (eq == std::string::npos) continue;
				std::string k = ln.substr(0, eq), v = trim(ln.substr(eq + 1));
				if (k == "wpa_state") s.state = v;
				else if (k == "ssid") s.ssid = unescape(v);
				else if (k == "ip_address") s.ip = v;
			}
			s.connected = s.state == "COMPLETED";
			return s;
		}

		// ---- wifi.conf ------------------------------------------------------------------------------------------
		struct Block { std::string text, ssid; int priority = 0; };

		static std::string ssidOf(const std::string& v)
		{
			if (v.size() >= 2 && v[0] == '"')
			{
				size_t e = v.rfind('"');	// as wpa_supplicant reads it: up to the last quote
				return e > 0 ? v.substr(1, e - 1) : "";
			}
			std::string r;	// hex
			for (size_t i = 0; i + 1 < v.size(); i += 2) r += (char)strtol(v.substr(i, 2).c_str(), nullptr, 16);
			return r;
		}

		static void readConf(const std::string& path, std::string& head, std::vector<Block>& blocks)
		{
			std::ifstream f(path);
			std::string ln;
			Block* cur = nullptr;
			while (std::getline(f, ln))
			{
				std::string t = trim(ln);
				if (!cur && t.compare(0, 8, "network=") == 0) { blocks.push_back(Block()); cur = &blocks.back(); }
				if (!cur) { head += ln + "\n"; continue; }
				cur->text += ln + "\n";
				size_t eq = t.find('=');
				if (eq != std::string::npos)
				{
					std::string k = trim(t.substr(0, eq)), v = trim(t.substr(eq + 1));
					if (k == "ssid") cur->ssid = ssidOf(v);
					else if (k == "priority") cur->priority = atoi(v.c_str());
				}
				if (t == "}") cur = nullptr;
			}
		}

		static bool writeConf(const std::string& path, const std::string& text)
		{
			std::string tmp = path + ".new";
			FILE* w = fopen(tmp.c_str(), "w");
			if (!w) return false;
			bool ok = fwrite(text.data(), 1, text.size(), w) == text.size();
			ok &= fflush(w) == 0 && fsync(fileno(w)) == 0;
			fclose(w);
			if (!ok || rename(tmp.c_str(), path.c_str()) != 0) { unlink(tmp.c_str()); return false; }
			return true;
		}

		std::vector<std::string> savedNames(const std::string& conf)
		{
			std::string head;
			std::vector<Block> blocks;
			readConf(conf, head, blocks);
			std::vector<std::string> r;
			for (auto& b : blocks) if (!b.ssid.empty()) r.push_back(b.ssid);
			return r;
		}

		bool passwordOk(const std::string& p)
		{
			if (p.size() < 8 || p.size() > 63) return false;
			for (unsigned char c : p) if (c < 0x20 || c > 0x7e) return false;
			return true;
		}

		bool remember(const std::string& conf, const Network& n, const std::string& password)
		{
			if (n.ssid.empty() || n.unsupported || (n.secured && !passwordOk(password))) return false;
			std::string head;
			std::vector<Block> blocks;
			readConf(conf, head, blocks);
			int top = 0;
			for (auto& b : blocks) top = std::max(top, b.priority);
			bool printable = true;
			for (unsigned char c : n.ssid) if (c < 0x20 || c > 0x7e) printable = false;
			std::string ssid;
			if (printable) ssid = "\"" + n.ssid + "\"";
			else { char h[3]; for (unsigned char c : n.ssid) { snprintf(h, sizeof h, "%02x", c); ssid += h; } }
			std::string b = "network={\n\tssid=" + ssid + "\n";
			if (!n.secured) b += "\tkey_mgmt=NONE\n";
			else
			{
				b += "\tpsk=\"" + password + "\"\n";
				if (n.saeOnly) b += "\tkey_mgmt=SAE\n\tieee80211w=2\n";
			}
			b += "\tpriority=" + std::to_string(top + 1) + "\n}\n";
			std::string out = head;
			if (!out.empty() && out.back() != '\n') out += "\n";
			out += b;
			for (auto& o : blocks) if (o.ssid != n.ssid) out += o.text;
			return writeConf(conf, out);
		}

		bool forget(const std::string& conf, const std::string& ssid)
		{
			std::string head;
			std::vector<Block> blocks;
			readConf(conf, head, blocks);
			std::string out = head;
			bool found = false;
			for (auto& o : blocks) { if (o.ssid == ssid) found = true; else out += o.text; }
			return found && writeConf(conf, out);
		}

		// ---- background commands --------------------------------------------------------------------------------
		static std::string run(const std::string& cmd)
		{
			std::string r;
			FILE* p = popen((cmd + " 2>/dev/null").c_str(), "r");
			if (!p) return r;
			char buf[4096];
			size_t n;
			while ((n = fread(buf, 1, sizeof buf, p)) > 0) r.append(buf, n);
			pclose(p);
			return r;
		}

		Job::~Job()
		{
			if (th.joinable()) th.join();
		}

		void Job::start(const std::string& cmd, bool sync)
		{
			if (th.joinable()) th.join();
			active = true;
			done = false;
			if (sync)
			{
				result = run(cmd);
				done = true;
				return;
			}
			th = std::thread([this, cmd] {
				std::string r = run(cmd);
				std::lock_guard<std::mutex> l(m);
				result = r;
				done = true;
			});
		}

		bool Job::take(std::string& out)
		{
			if (!active || !done) return false;
			if (th.joinable()) th.join();
			std::lock_guard<std::mutex> l(m);
			out.swap(result);
			result.clear();
			active = false;
			return true;
		}
	}
}
