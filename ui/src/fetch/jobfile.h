#pragma once
// Downloads: what banana-ui and banana-fetch tell each other through files, shared by both so they can't disagree on
// the format. A job (banana/downloads/<time>.job on the card: what to download from which server, or which archive on
// the card to unpack, and where), what happened to each of its items (<job>.done, a line as each finishes, so a job cut
// short goes on from there), and the status line of the item in hand (/run/banana/fetch/status).
#include <cstdint>
#include <string>
#include <vector>

namespace banana
{
	namespace fetch
	{
		// fields of a tab-separated line: tabs, line ends and backslashes escaped
		std::string escapeField(const std::string& s);
		std::string unescapeField(const std::string& s);
		std::vector<std::string> splitFields(const std::string& line);
		std::string joinFields(const std::vector<std::string>& fields);

		struct JobItem
		{
			bool extract = false;		// an archive on the card to unpack; else a file to download
			int64_t size = -1;			// (get: -1 when the server didn't say)
			std::string remote, local;	// get: the server's path, the card's; extract: the archive, the folder it goes into
		};

		struct Job
		{
			std::string path;			// the job's file
			std::string title;			// for the screen: "3 files from NAS"
			std::string server;			// the server's id (downloads)
			bool unpack = false, deleteArchive = false, unwrap = true, ignoreMemory = false;
			bool keepZips = false;		// unpack, but not the zips: the folder's system plays its games zipped
			std::string tempRoot;		// the card's root: where unpacking's own folder goes
			std::string archivePasswordFile;	// in /run/banana: read once, then gone
			std::vector<JobItem> items;
		};
		bool readJob(const std::string& path, Job& j);
		// written beside and renamed over: whoever reads it sees all of it or none
		bool writeJob(const std::string& path, const Job& j);

		struct Outcome
		{
			int index = -1;
			std::string what;			// got, unpacked, packed (an archive left as it is), failed, skipped
			std::string detail;
		};
		std::vector<Outcome> readOutcomes(const std::string& path);
		void addOutcome(const std::string& path, const Outcome& o);

		std::vector<std::string> pendingJobs(const std::string& dir);	// *.job, oldest first
		std::vector<std::string> finishedJobs(const std::string& dir);	// *.finished, newest first

		// "<state> <numbers...>|<name>|<why>"
		struct Status
		{
			std::string state;			// connect, get, retry, unpack, need, done, stopped, error ("" none)
			int i = 0, n = 0;			// the item in hand (from 1), of how many
			int64_t done = 0, total = -1, speed = 0, eta = -1;
			int attempt = 0, retries = 0, seconds = 0, percent = 0;
			int downloaded = 0, unpacked = 0, failed = 0, packed = 0;
			std::string name, why;
		};
		std::string formatStatus(const Status& s);
		Status parseStatus(const std::string& line);
		// a job's "done": its title, and its items downloaded, unpacked, failed and left packed
		Status summary(const Job& j, const std::vector<Outcome>& outs);
	}
}
