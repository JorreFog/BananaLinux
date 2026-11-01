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
