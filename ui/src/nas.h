#pragma once
// banana-ui: what Files' server pages (settings_servers.cpp) need besides drawing. banana-fetch's session (the process
// that logs in to a server and lists it; its answers read on a thread), and the runner of the jobs (started, and
// stopped while a game runs). The folders a download goes to by default are sysfolders.h's.
#include "fetch/jobfile.h"
#include "fetch/paths.h"
#include "fetch/servers.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <thread>
#include <vector>

namespace banana
{
	namespace nas
	{
		// banana-fetch serve: one request at a time, its answer read on a thread (sync: at once, for the headless test)
		class Session
		{
		public:
			~Session() { stop(); }
			// called on the reader's thread once an answer is all in (the menu's loop woken to take it, not polling)
			std::function<void()> onAnswer;
			bool start(const std::string& prog, const std::string& dataDir, bool sync);
			void stop();
			// the helper still there: it goes by itself after its idle minutes, or when it fails (reaped here then)
			bool alive();
			// false while an answer is still to come, or with no session
			bool ask(const std::vector<std::string>& fields);
			bool busy() const;
			bool answered() const;	// an answer is in, not taken yet
			// the answer, once it is all in: its rows, and its last line ("ok ...", "end ...", "err <word> <message>...")
			bool take(std::vector<std::vector<std::string>>& rows, std::vector<std::string>& last);
		private:
			pid_t pid = -1;
			int in = -1, out = -1;
			bool sync = false, reaped = false;
			std::atomic<bool> gone{ false };	// its end seen (EOF on its answers, a write it didn't take)
			std::thread reader;
			mutable std::mutex mu;
			bool waiting = false, ready = false;
			std::vector<std::vector<std::string>> rowsIn;
			std::vector<std::string> lastIn;
			std::string buf;
			void readAnswer();
		};

		// the runner: transfer.pid's process, when it is alive
		pid_t runner(const fetch::Paths& p);
		// started detached (sync: run to the end now, the headless test)
		void startRunner(const std::string& prog, const std::string& dataDir, bool sync);
		// stopped (SIGSTOP, its process group with its 7zz) while a game runs, and let go on after
		void pauseRunner(const fetch::Paths& p, bool pause);
		// asked to stop (SIGTERM; one paused is let go on first, to hear it): the parts and the job are kept
		void stopRunner(const fetch::Paths& p);
		std::string newJobPath(const fetch::Paths& p);
		// a job given up on (the job page's Cancel), its runner gone: its file and its outcomes deleted, with what its
		// unfinished downloads left on the card (their .part files) and an archive's password file
		void dropJob(const std::string& jobPath);
		// a password for a job, in a file of /run/banana only the runner reads (once)
		bool writeSecret(const std::string& path, const std::string& secret);
	}
}
