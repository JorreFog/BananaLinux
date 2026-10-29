#pragma once
// banana-ui: what the shelf's Files does to the cards, without drawing anything (settings_files.cpp asks and draws).
// The cards: this one (the data partition: the ROM folder's parent) and the RG DS Plus's second SD card while it is
// in. A folder listed, a name checked, a folder made, an entry renamed; and the job: files and folders copied, moved
// or deleted on a thread of its own, its progress read under a lock, stopped between two blocks of a file.
//   a copy is written beside its place under a hidden name (.banana-copy-<name>, cut to fit a name), sent to the card
//   8 MiB at a time as it goes, given its source's time, flushed and renamed into place: a cut-off copy never leaves
//   half a file under the real name, and its leftover is deleted the next time its folder is listed
//   a move on one card is a rename; between the two cards a copy, the source deleted once its copy's name is on the
//   card (its folder flushed: a leftover is never the only copy)
//   the room and FAT32's 4 GB a file are checked before the first byte is written; the cards pasted on and taken from
//   are flushed before the outcome is told (and a folder made or an entry renamed at once)
//   the card pasted on taken out mid-way (its path then the mount point's own folder in /run, the RAM): what is made
//   is checked to be on its file system, and the job ends there, no source deleted
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <thread>
#include <vector>

namespace banana
{
	namespace files
	{
		// a card: id "data" (This card) or "sd2" (Second SD card, mounted at /run/sd2; a folder standing in for it in
		// the headless test: BANANA_SD2). dev: its file system's, as it was when the card was found mounted (a paste is
		// done on that one or not at all: Job::start)
		struct Card { std::string id, label, root; dev_t dev = 0; };
		std::vector<Card> cards(const std::string& romRoot);

		// a folder's entry. Its size and time are read when it is shown (known), not for every name of the folder
		struct Entry { std::string name; bool dir = false, known = false; int64_t size = 0, mtime = 0; };
		// a folder's entries, folders first, then by name (case aside); hidden ones left out. tidy: the leftovers of a
		// copy cut off (no job running) deleted on the way. false, and why, when it can't be read
		bool list(const std::string& dir, std::vector<Entry>& out, std::string& why, bool tidy);
		void details(const std::string& dir, Entry& e);
		int count(const std::string& dir);	// a folder's entries as list() shows them; -1 when it can't be read

		// the room on a card: free and total bytes, its cluster (a file takes whole ones), FAT32 (4 GB a file at most)
		struct Space { uint64_t free = 0, total = 0, cluster = 0; bool fat32 = false; };
		bool space(const std::string& path, Space& s);

		std::string baseName(const std::string& path);
		std::string parent(const std::string& path);
		// path is dir, or somewhere under it
		bool inside(const std::string& path, const std::string& dir);
		bool exists(const std::string& path);	// (lstat: on exFAT and FAT32 as blind to case as the card)
		// a name a card can hold and a computer can open: "" when it can, else why not. dir: the folder it goes in (its
		// card's limit: 255 UTF-16 characters on exFAT and FAT32, 255 bytes on Linux's own file systems)
		std::string nameProblem(const std::string& name, const std::string& dir = "");
		// name, or "name (2).ext", "name (3).ext"... while one is taken in dir (a folder's name has no extension)
		std::string freeName(const std::string& dir, const std::string& name, bool folder = false);
		// the systems' folders under roms/ (systems.conf's folder, or the id; an app's absolute folder left out),
		// lower case
		std::vector<std::string> systemFolders(const std::string& systemsConf);
		// the card's own folders stay where they are: a card itself, banana, bios, roms, saves and states at the data
		// card's root, roms and each system's folder in it (on the second card too). Why, or "" for any other entry
		std::string keptWhy(const std::string& path, const std::vector<Card>& cards, const std::vector<std::string>& systems);
		// "512 B", "340 KB", "1.2 MB", "18 GB"
		std::string bytes(uint64_t n);
		// the names of sources already taken in dest (a source that is its own target isn't counted: a copy of it gets a
		// new name, a move of it is nothing to do)
		std::vector<std::string> clashes(const std::vector<std::string>& sources, const std::string& dest);
		// why a paste of sources into dest can't be, "" when it can (a folder into itself or one of its own)
		std::string pasteProblem(const std::vector<std::string>& sources, const std::string& dest);
		bool makeFolder(const std::string& dir, const std::string& name, std::string& why);
		// an entry renamed in its folder; a change of case alone goes through a hidden name (on exFAT and FAT32 the new
		// name is the old one, and a rename to it does nothing)
		bool renameEntry(const std::string& dir, const std::string& from, const std::string& to, std::string& why);

		enum class Op { COPY, MOVE, DELETE };
		// what a paste does with a name already taken: a new name for the copy, the old one replaced (a folder: the two
		// merged, the files in both replaced), or the source left out
		enum class Clash { KEEP_BOTH, REPLACE, SKIP };

		struct Progress
		{
			enum State { IDLE, COUNTING, WORKING, DONE } state = IDLE;
			Op op = Op::COPY;
			std::string current;			// the file in hand
			uint64_t bytes = 0, total = 0;	// copied of all to copy (0 for a delete, and a move on one card)
			int done = 0, items = 0;		// files (a delete: entries) done of all
			int failed = 0, skipped = 0;
			int sourcesDone = 0;			// the sources asked for that were done whole (nothing under them failed or was left out)
			bool stopped = false;			// Stop cut it short: what was done stays (a Stop after the last byte is no stop)
			bool unwritten = false;			// the card refused a write when the paste was flushed to it at the end
			std::string error;				// the first thing that went wrong, said plainly; "" for none
			std::vector<std::string> placed;	// the names the sources got in the folder pasted into
			double seconds = 0;				// since it started (at progress())
		};

		// one at a time; the pages start it, show its progress and stop it. While it works, /run/banana/files.pid
		// holds bananad's idle sleep
		class Job
		{
		public:
			~Job();	// stopped and waited for (banana-ui told to quit in the middle of one)
			// sources: full paths; dest: the folder pasted into (none for DELETE); sync: done here and now (the headless
			// test). done: called on the job's thread once it is over (the host's loop woken). dev: the file system dest
			// must be on (its Card's dev; 0: whatever it is on): its card taken out since, nothing is pasted, and what is
			// made on another mid-way stops the job (the mount point's own folder in /run is the RAM)
			void start(Op op, const std::vector<std::string>& sources, const std::string& dest, Clash clash, bool sync, std::function<void()> done = nullptr, dev_t dev = 0);
			void stop() { halt = true; }
			bool running() const;
			Progress progress() const;
		private:
			struct Run;
			std::thread th;
			mutable std::mutex mu;
			Progress p;
			std::atomic<bool> halt{ false };
			double started = 0;
			void run(Op op, std::vector<std::string> sources, std::string dest, Clash clash, std::function<void()> done, dev_t dev);
		};
	}
}
