#pragma once
// banana-fetch: one file from a server to the card. It streams into <name>.part beside
// where the file goes and resumes from the part's size; it tries again when the network drops (a streak of failures
// ends as soon as bytes arrive again, so a flaky Wi-Fi doesn't use the tries up, and the handheld's sleep, Wi-Fi
// unloaded and loaded again, is waited out); a link quiet for 30 s is given up on (the watchdog aborts it); the part
// is renamed into place when its size is right.
#include "remote.h"
#include <atomic>
#include <mutex>

namespace banana
{
	namespace fetch
	{
		struct Item
		{
			std::string remote, local;
			int64_t size = -1;		// -1: the server didn't say
		};
		extern const char* const PART;	// ".part"

		// how a download is going: written by it, read by the job's status line (another thread)
		struct Progress
		{
			enum State { CONNECTING, DOWNLOADING, RETRY, FINISHING, DONE };
			std::atomic<int> state{ CONNECTING }, attempt{ 0 }, retries{ 0 };
			std::atomic<int64_t> done{ 0 }, total{ -1 }, speed{ 0 };
			std::atomic<double> retryAt{ 0 };	// RETRY: when the next try starts (steadyMs)
			void start(int64_t total, int64_t done);
			void add(int64_t n);				// n more bytes; the speed is smoothed about twice a second
			void setDone(int64_t done);			// (after a jump: the sampler starts again)
			void retry(const std::string& why, int seconds, int attempt);
			std::string why();					// what the retry is about
			int64_t eta() const;				// seconds, -1 unknown
		private:
			std::mutex mu;
			std::string lastWhy;
			double sampleAt = 0;
			int64_t sampleVal = 0;
		};

		double steadyMs();

		class Downloader
		{
		public:
			// a new connected backend, for the first file and after a failure: nullptr and r why not
			std::function<std::unique_ptr<Backend>(Result& r)> connect;
			std::atomic<bool>* stop = nullptr;
			int retries = 5;						// failures in a row tried again
			std::vector<int> delays = { 3, 10, 30, 60 };	// seconds before each, the last one for the rest
			int stallSeconds = 30;
			int64_t rate = 0;						// bytes a second at most (the tests: a drop in the middle); 0 none
			Result download(const Item& it, Progress& p);
			void close() { be.reset(); }
		private:
			std::unique_ptr<Backend> be;
			Result attempt(const Item& it, const std::string& part, Progress& p);
			bool stopped() const { return stop && *stop; }
		};

		int64_t fileSize(const std::string& path);	// -1 when it isn't there
		bool makeDirs(const std::string& dir);
		std::string dirName(const std::string& path);
	}
}
