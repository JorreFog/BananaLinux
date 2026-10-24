#include "nas.h"
#include "fetch/names.h"
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace banana
{
	namespace nas
	{
		// banana-fetch's environment: banana-ui's, with the card's banana folder it was given (--data)
		static std::vector<std::string> envWith(const std::string& dataDir)
		{
			std::vector<std::string> env;
			for (char** e = environ; e && *e; e++)
				if (strncmp(*e, "BANANA_DATA=", 12) != 0) env.push_back(*e);
			env.push_back("BANANA_DATA=" + dataDir);
			return env;
		}

		static std::vector<char*> pointers(std::vector<std::string>& v)
		{
			std::vector<char*> p;
			for (auto& s : v) p.push_back((char*)s.c_str());
			p.push_back(nullptr);
			return p;
		}

		// ---- the session ---------------------------------------------------------------------------------------------
		bool Session::start(const std::string& prog, const std::string& dataDir, bool s)
		{
			stop();
			sync = s;
			int a[2], b[2];
			if (pipe2(a, O_CLOEXEC) != 0) return false;
			if (pipe2(b, O_CLOEXEC) != 0) { close(a[0]); close(a[1]); return false; }
			// (everything exec needs made before the fork: the menu has threads)
			std::vector<std::string> env = envWith(dataDir), args = { prog, "serve" };
			std::vector<char*> envp = pointers(env), argv = pointers(args);
			pid_t p = fork();
			if (p < 0) { close(a[0]); close(a[1]); close(b[0]); close(b[1]); return false; }
			if (p == 0)
			{
				dup2(a[0], 0);
				dup2(b[1], 1);
				execve(argv[0], argv.data(), envp.data());
				_exit(127);
			}
			close(a[0]);
			close(b[1]);
			in = a[1];
			out = b[0];
			pid = p;
			reaped = false;
			gone = false;
			buf.clear();
			return true;
		}

		void Session::stop()
		{
			if (in >= 0) { close(in); in = -1; }	// (its stdin ends: it goes)
			if (pid > 0 && !reaped) kill(pid, SIGTERM);	// (now, even in the middle of an answer; a pid reaped may be another's)
			if (reader.joinable()) reader.join();
			if (pid > 0 && !reaped) while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
			pid = -1;
			if (out >= 0) { close(out); out = -1; }
			std::lock_guard<std::mutex> l(mu);
			waiting = ready = false;
		}

		bool Session::alive()
		{
			if (pid <= 0 || gone) return false;
			if (reaped || waitpid(pid, nullptr, WNOHANG) == pid) { reaped = true; gone = true; }
			return !gone;
		}

		bool Session::busy() const
		{
			std::lock_guard<std::mutex> l(mu);
			return waiting;
		}

		bool Session::answered() const
		{
			std::lock_guard<std::mutex> l(mu);
			return ready;
		}

		bool Session::ask(const std::vector<std::string>& fields)
		{
			if (pid <= 0 || busy()) return false;
			if (reader.joinable()) reader.join();
			{
				std::lock_guard<std::mutex> l(mu);
				waiting = true;
				ready = false;
				rowsIn.clear();
				lastIn.clear();
			}
			std::string ln = fetch::joinFields(fields) + "\n";
			for (size_t w = 0; w < ln.size();)
			{
				ssize_t k = write(in, ln.data() + w, ln.size() - w);
				if (k < 0 && errno == EINTR) continue;
				if (k <= 0)
				{
					gone = true;
					std::lock_guard<std::mutex> l(mu);
					lastIn = { "err", "network", "The downloads helper stopped." };
					ready = true;
					waiting = false;
					return true;
				}
				w += (size_t)k;
			}
			if (sync) readAnswer();
			else reader = std::thread([this] { readAnswer(); });
			return true;
		}

		// lines until the last of the answer ("ok", "end" or "err")
		void Session::readAnswer()
		{
			std::vector<std::vector<std::string>> rows;
			std::vector<std::string> last;
			for (;;)
			{
				size_t nl = buf.find('\n');
				if (nl != std::string::npos)
				{
					std::vector<std::string> f = fetch::splitFields(buf.substr(0, nl));
					buf.erase(0, nl + 1);
					if (f[0] == "ok" || f[0] == "end" || f[0] == "err") { last = f; break; }
					rows.push_back(f);
					continue;
				}
				char tmp[8192];
				ssize_t k = read(out, tmp, sizeof tmp);
				if (k < 0 && errno == EINTR) continue;
				if (k <= 0) { gone = true; last = { "err", "network", "The downloads helper stopped." }; break; }
				buf.append(tmp, (size_t)k);
			}
			{
				std::lock_guard<std::mutex> l(mu);
				rowsIn.swap(rows);
				lastIn.swap(last);
				ready = true;
				waiting = false;
			}
			if (onAnswer && !sync) onAnswer();
		}

		bool Session::take(std::vector<std::vector<std::string>>& rows, std::vector<std::string>& last)
		{
			std::lock_guard<std::mutex> l(mu);
			if (!ready) return false;
			rows.swap(rowsIn);
			last.swap(lastIn);
			ready = false;
			return true;
		}

		// ---- cards and folders -----------------------------------------------------------------------------------
		static bool mounted(const std::string& path)
		{
			std::ifstream f("/proc/mounts");
			std::string dev, at, rest;
			while (f >> dev >> at && std::getline(f, rest)) if (at == path) return true;
			return false;
		}

		std::vector<Card> cards(const std::string& romRoot)
		{
			std::vector<Card> out;
			std::string root = romRoot.size() > 5 && romRoot.compare(romRoot.size() - 5, 5, "/roms") == 0 ? romRoot.substr(0, romRoot.size() - 5) : romRoot;
			out.push_back({ "data", "This card", root, romRoot });
			// the RG DS Plus's second card, while it is in (bananad's sd2 script mounts it at /run/sd2); a folder standing in
			// for it in the headless test (BANANA_SD2)
			const char* t = getenv("BANANA_SD2");
			std::string sd2 = t && *t ? t : "/run/sd2";
			if ((t && *t) || mounted(sd2)) out.push_back({ "sd2", "Second SD card", sd2, sd2 + "/roms" });
			return out;
		}

		// what a server's folder for a system may be called (the usual names of Banana's systems)
		static const struct { const char* id; const char* names; } ALIASES[] = {
			{ "nds", "nds,ds,nintendo ds" }, { "gba", "gba,game boy advance,gameboy advance" },
			{ "gbc", "gbc,game boy color,gameboy color" }, { "gb", "gb,game boy,gameboy" },
			{ "nes", "nes,fc,famicom,nintendo entertainment system" }, { "snes", "snes,sfc,super nintendo,super famicom" },
			{ "megadrive", "md,megadrive,mega drive,genesis,sega genesis" }, { "psx", "psx,ps,ps1,playstation" },
			{ "n64", "n64,nintendo 64" }, { "pico-8", "pico,pico8,pico-8" }, { "ports", "ports" },
		};

		static void addNames(std::vector<std::string>& to, const std::string& list)
		{
			std::stringstream ss(list);
			std::string n;
			while (std::getline(ss, n, ','))
			{
				n = fetch::lower(fetch::trim(n));
				if (!n.empty() && std::find(to.begin(), to.end(), n) == to.end()) to.push_back(n);
			}
		}

		std::vector<Folder> folders(const std::string& systemsConf)
		{
			std::vector<Folder> out;
			std::ifstream f(systemsConf);
			std::string ln, absolute;
			Folder* cur = nullptr;
			auto done = [&] { if (cur && !absolute.empty()) out.pop_back(); cur = nullptr; absolute.clear(); };
			while (std::getline(f, ln))
			{
				ln = fetch::trim(ln);
				if (ln.empty() || ln[0] == '#') continue;
				if (ln[0] == '[' && ln.back() == ']')
				{
					done();
					out.push_back(Folder());
					cur = &out.back();
					cur->system = fetch::lower(ln.substr(1, ln.size() - 2));
					cur->rel = "roms/" + cur->system;
					cur->label = cur->system;
					continue;
				}
				size_t eq = ln.find('=');
				if (!cur || eq == std::string::npos) continue;
				std::string k = fetch::trim(ln.substr(0, eq)), v = fetch::trim(ln.substr(eq + 1));
				if (k == "name") cur->label = v;
				else if (k == "folder") { if (!v.empty() && v[0] == '/') absolute = v; else cur->rel = "roms/" + v; }	// (an app's own: no games go there)
				else if (k == "aliases") addNames(cur->names, v);
				else if (k == "ext")
				{
					std::istringstream es(v);
					std::string e;
					while (es >> e) cur->exts.push_back(fetch::lower(e[0] == '.' ? e.substr(1) : e));
				}
			}
			done();
			for (auto& fo : out)
			{
				fo.names.insert(fo.names.begin(), fetch::lower(fo.rel.substr(5)));
				addNames(fo.names, fo.system + "," + fo.label);
				for (auto& a : ALIASES) if (fo.system == a.id) addNames(fo.names, a.names);
				fo.playsZip = std::find(fo.exts.begin(), fo.exts.end(), "zip") != fo.exts.end();
			}
			Folder bios;
			bios.label = "BIOS files";
			bios.rel = "bios";
			bios.names = { "bios", "system", "firmware" };
			out.push_back(bios);
			Folder roms;
			roms.label = "The roms folder";
			roms.rel = "roms";
			out.push_back(roms);
			return out;
		}

		int suggest(const std::vector<Folder>& f, const std::string& name, const std::string& remoteDir)
		{
			std::string n = fetch::lower(fetch::stripArchive(name));
			size_t dot = n.rfind('.');
			if (!n.empty() && dot != std::string::npos)
			{
				std::string ext = n.substr(dot + 1);
				int hit = -1, count = 0;
				for (int i = 0; i < (int)f.size(); i++)
					for (auto& e : f[i].exts)
						if (e == ext && ext != "zip")
						{
							if (f[i].system == ext) return i;	// (an extension two systems take: the one named after it)
							count++;
							hit = i;
						}
				if (count == 1) return hit;
			}
			// the server's folders, the deepest first
			std::vector<std::string> parts;
			std::stringstream ss(remoteDir);
			std::string p;
			while (std::getline(ss, p, '/')) if (!p.empty()) parts.push_back(fetch::lower(p));
			for (int k = (int)parts.size() - 1; k >= 0; k--)
				for (int i = 0; i < (int)f.size(); i++)
					if (std::find(f[i].names.begin(), f[i].names.end(), parts[k]) != f[i].names.end()) return i;
			return -1;
		}

		std::vector<LocalEntry> localEntries(const std::string& dir)
		{
			std::vector<LocalEntry> out;
			DIR* d = opendir(dir.c_str());
			if (!d) return out;
			while (dirent* e = readdir(d))
			{
				std::string n = e->d_name;
				if (n.empty() || n[0] == '.') continue;	// (hidden ones, and .banana-unpack)
				std::string p = dir + "/" + n;
				struct stat st;
				if (stat(p.c_str(), &st) != 0) continue;
				LocalEntry le;
				le.name = n;
				if (S_ISDIR(st.st_mode)) { le.dir = true; out.push_back(le); continue; }
				if (!fetch::isArchive(n)) continue;
				for (auto& v : fetch::volumes(p)) if (stat(v.c_str(), &st) == 0) le.size += st.st_size;
				out.push_back(le);
			}
			closedir(d);
			std::stable_sort(out.begin(), out.end(), [](const LocalEntry& a, const LocalEntry& b)
			{
				if (a.dir != b.dir) return a.dir;
				return fetch::lower(a.name) < fetch::lower(b.name);
			});
			return out;
		}

		// ---- the runner --------------------------------------------------------------------------------------------
		pid_t runner(const fetch::Paths& p)
		{
			std::ifstream f(p.pid());
			int v = 0;
			f >> v;
			if (v <= 0 || kill(v, 0) != 0) return 0;
			// (one that ended and isn't reaped yet, a zombie, is gone too: the state after its name in /proc/<pid>/stat)
			std::ifstream st("/proc/" + std::to_string(v) + "/stat");
			std::string s((std::istreambuf_iterator<char>(st)), std::istreambuf_iterator<char>());
			size_t nameEnd = s.rfind(')');
			return nameEnd != std::string::npos && nameEnd + 2 < s.size() && s[nameEnd + 2] == 'Z' ? 0 : v;
		}

		// (settings.cpp's spawn() does the same fork, but this one runs with the card's folder in its environment, and
		// the headless test waits for it)
		void startRunner(const std::string& prog, const std::string& dataDir, bool sync)
		{
			std::vector<std::string> env = envWith(dataDir), args = { prog, "run" };
			std::vector<char*> envp = pointers(env), argv = pointers(args);
			pid_t p = fork();
			if (p < 0) return;
			if (p == 0)
			{
				// detached: its own session (a process group banana-ui can stop as a whole while a game runs), not our child
				if (!sync && fork() != 0) _exit(0);
				setsid();
				execve(argv[0], argv.data(), envp.data());
				_exit(127);
			}
			while (waitpid(p, nullptr, 0) < 0 && errno == EINTR) {}
		}

		void pauseRunner(const fetch::Paths& p, bool pause)
		{
			pid_t r = runner(p);
			if (!r) return;
			kill(-r, pause ? SIGSTOP : SIGCONT);
			if (!pause) kill(r, SIGCONT);
		}

		void stopRunner(const fetch::Paths& p)
		{
			pid_t r = runner(p);
			if (!r) return;
			kill(r, SIGTERM);
			pauseRunner(p, false);
		}

		void dropJob(const std::string& jobPath)
		{
			fetch::Job j;
			if (fetch::readJob(jobPath, j))
			{
				std::vector<fetch::Outcome> outs = fetch::readOutcomes(jobPath + ".done");
				for (size_t i = 0; i < j.items.size(); i++)
				{
					if (j.items[i].extract) continue;
					bool got = false;
					for (auto& o : outs) if (o.index == (int)i && o.what != "failed") got = true;
					if (!got) unlink((j.items[i].local + ".part").c_str());
				}
				if (!j.archivePasswordFile.empty()) unlink(j.archivePasswordFile.c_str());
			}
			unlink(jobPath.c_str());
			unlink((jobPath + ".done").c_str());
		}

		std::string newJobPath(const fetch::Paths& p)
		{
			mkdir(p.data.c_str(), 0755);
			mkdir(p.jobs().c_str(), 0755);
			char t[32];
			time_t now = time(nullptr);
			struct tm tm;
			localtime_r(&now, &tm);
			strftime(t, sizeof t, "%Y%m%d-%H%M%S", &tm);
			for (int n = 1;; n++)
			{
				std::string path = p.jobs() + "/" + t + "-" + std::to_string(n) + ".job";
				struct stat st;
				if (stat(path.c_str(), &st) != 0 && stat((path.substr(0, path.size() - 4) + ".finished").c_str(), &st) != 0) return path;
			}
		}

		bool writeSecret(const std::string& path, const std::string& secret)
		{
			mkdir(path.substr(0, path.rfind('/')).c_str(), 0755);
			int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
			if (fd < 0) return false;
			bool ok = write(fd, secret.data(), secret.size()) == (ssize_t)secret.size();
			close(fd);
			return ok;
		}
	}
}
