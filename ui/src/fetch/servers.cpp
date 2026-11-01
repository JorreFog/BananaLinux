#include "servers.h"
#include "jobfile.h"
#include "names.h"
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace banana
{
	namespace fetch
	{
		// (a value can't break the file: no line ends in it)
		static std::string oneLine(std::string v)
		{
			for (auto& c : v) if (c == '\n' || c == '\r') c = ' ';
			return v;
		}

		std::string ServerRec::target(const std::string& remoteDir) const
		{
			for (auto& t : targets) if (t.first == remoteDir) return t.second;
			return "";
		}

		std::vector<ServerRec> loadServers(const std::string& path)
		{
			std::vector<ServerRec> out;
			std::ifstream f(path);
			std::string ln;
			ServerRec* cur = nullptr;
			while (std::getline(f, ln))
			{
				std::string t = trim(ln);
				if (t.empty() || t[0] == '#') continue;
				if (t[0] == '[' && t.back() == ']')
				{
					out.push_back(ServerRec());
					cur = &out.back();
					cur->id = t.substr(1, t.size() - 2);
					continue;
				}
				// key = value (a target's value: the remote folder and where its downloads went, two escaped fields, as either
				// may have " = " in it)
				size_t eq = t.find(" = ");
				std::string k, v;
				if (eq != std::string::npos) { k = trim(t.substr(0, eq)); v = t.substr(eq + 3); }
				else if (t.back() == '=') k = trim(t.substr(0, t.size() - 1));
				else continue;
				v = trim(v);
				if (!cur) continue;
				if (k == "name") cur->name = v;
				else if (k == "protocol") cur->protocol = v;
				else if (k == "host") cur->host = v;
				else if (k == "port") cur->port = atoi(v.c_str());
				else if (k == "guest") cur->guest = v == "yes";
				else if (k == "user") cur->user = v;
				else if (k == "remember") cur->remember = v != "no";
				else if (k == "password") cur->sealed = v;
				else if (k == "domain") cur->domain = v;
				else if (k == "start") cur->start = v;
				else if (k == "tls") cur->tls = v == "yes";
				else if (k == "encoding") cur->encoding = v.empty() ? "utf-8" : v;
				else if (k == "hostkey") cur->hostkey = v;
				else if (k == "target")
				{
					std::vector<std::string> f = splitFields(v);
					if (f.size() == 2 && !f[0].empty()) cur->targets.push_back({ f[0], f[1] });
				}
			}
			return out;
		}

		bool saveServers(const std::string& path, const std::vector<ServerRec>& list)
		{
			std::ostringstream o;
			o << "# Banana's file servers: Files adds and changes them. A password is kept sealed (\"v2:...\"): it opens on this\n"
				"# handheld only. Take the line out and Files asks for it again. target = <server folder> TAB <card>:<folder>\n";
			for (auto& s : list)
			{
				o << "\n[" << s.id << "]\n";
				o << "name = " << oneLine(s.name) << "\nprotocol = " << oneLine(s.protocol) << "\nhost = " << oneLine(s.host) << "\n";
				o << "port = " << s.port << "\nguest = " << (s.guest ? "yes" : "no") << "\nuser = " << oneLine(s.user) << "\n";
				o << "remember = " << (s.remember ? "yes" : "no") << "\n";
				if (!s.sealed.empty()) o << "password = " << s.sealed << "\n";
				o << "domain = " << oneLine(s.domain) << "\nstart = " << oneLine(s.start) << "\ntls = " << (s.tls ? "yes" : "no") << "\n";
				o << "encoding = " << oneLine(s.encoding) << "\nhostkey = " << oneLine(s.hostkey) << "\n";
				for (auto& t : s.targets) o << "target = " << joinFields({ t.first, t.second }) << "\n";
			}
			std::string text = o.str(), tmp = path + ".new";
			int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
			if (fd < 0) return false;
			bool ok = write(fd, text.data(), text.size()) == (ssize_t)text.size();
			ok &= fsync(fd) == 0;
			ok &= close(fd) == 0;
			if (!ok || rename(tmp.c_str(), path.c_str()) != 0) { unlink(tmp.c_str()); return false; }
			return true;
		}

		bool updateServers(const std::string& path, const std::function<bool(std::vector<ServerRec>&)>& fn)
		{
			int lock = ::open((path + ".lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
			if (lock >= 0) flock(lock, LOCK_EX);
			std::vector<ServerRec> list = loadServers(path);
			bool ok = !fn(list) || saveServers(path, list);
			if (lock >= 0) close(lock);	// (the lock goes with it)
			return ok;
		}

		ServerRec* findServer(std::vector<ServerRec>& list, const std::string& id)
		{
			for (auto& s : list) if (s.id == id) return &s;
			return nullptr;
		}
	}
}
