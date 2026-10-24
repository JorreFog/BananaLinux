#pragma once
// banana-ui: what the Downloads pages (settings_downloads.cpp) need besides drawing. banana-fetch's session (the process
// that logs in to a server and lists it; its answers read on a thread), the cards and the folders on them a download
// can go to (systems.conf's systems, and the one a file looks like it belongs to), the archives on the cards, and the
// runner of the jobs (started, and stopped while a game runs).
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

		// a card: this one (the data partition) and the RG DS Plus's second SD card while it is in
		struct Card { std::string id, label, root, roms; };	// id "data" or "sd2"; roms: <root>/roms
		std::vector<Card> cards(const std::string& romRoot);

		// a folder a download can go to: a system's (roms/<folder>), the BIOS folder, the roms folder itself
		struct Folder
		{
			std::string label, rel, system;		// rel: from the card's root ("roms/nds", "bios", "roms")
			std::vector<std::string> exts, names;	// names: what a server's folder for it may be called (lower case)
			bool playsZip = false;					// its emulator takes the games zipped (systems.conf's ext has zip)
		};
		std::vector<Folder> folders(const std::string& systemsConf);
		// the folder a file belongs to: by its extension (an archive's suffix looked through; zip, and an extension two
		// systems share, say nothing), then by the server's folder names from the deepest up. -1: none
		int suggest(const std::vector<Folder>& f, const std::string& name, const std::string& remoteDir);

		// a folder's subfolders and archives (first volumes only, the size all the volumes'), hidden ones left out
		struct LocalEntry { std::string name; bool dir = false; int64_t size = 0; };
		std::vector<LocalEntry> localEntries(const std::string& dir);

		// the runner: transfer.pid's process, when it is alive
		pid_t runner(const fetch::Paths& p);
		// started detached (sync: run to the end now, the headless test)
		void startRunner(const std::string& prog, const std::string& dataDir, bool sync);
		// stopped (SIGSTOP, its process group with its 7zz) while a game runs, and let go on after
		void pauseRunner(const fetch::Paths& p, bool pause);
		// asked to stop (SIGTERM; one paused is let go on first, to hear it): the parts and the job are kept
		void stopRunner(const fetch::Paths& p);
		std::string newJobPath(const fetch::Paths& p);
		// a job given up on (Downloads' Cancel), its runner gone: its file and its outcomes deleted, with what its
		// unfinished downloads left on the card (their .part files) and an archive's password file
		void dropJob(const std::string& jobPath);
		// a password for a job, in a file of /run/banana only the runner reads (once)
		bool writeSecret(const std::string& path, const std::string& secret);
	}
}
