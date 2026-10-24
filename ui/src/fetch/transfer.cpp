#include "transfer.h"
#include "names.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace banana
{
	namespace fetch
	{
		const char* const PART = ".part";

		double steadyMs()
		{
			return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		int64_t fileSize(const std::string& path)
		{
			struct stat st;
			return stat(path.c_str(), &st) == 0 ? (int64_t)st.st_size : -1;
		}

		std::string dirName(const std::string& path)
		{
			size_t slash = path.rfind('/');
			return slash == std::string::npos ? "." : slash == 0 ? "/" : path.substr(0, slash);
		}

		bool makeDirs(const std::string& dir)
		{
			struct stat st;
			if (dir.empty() || stat(dir.c_str(), &st) == 0) return !dir.empty() && S_ISDIR(st.st_mode);
			std::string up = dirName(dir);
			if (up != dir && !makeDirs(up)) return false;
			return mkdir(dir.c_str(), 0755) == 0 || errno == EEXIST;
		}

		// ---- progress -------------------------------------------------------------------------------------------------
		void Progress::start(int64_t t, int64_t d)
		{
			total = t;
			attempt = 0;
			speed = 0;
			state = CONNECTING;
			setDone(d);
		}

		void Progress::setDone(int64_t d)
		{
			done = d;
			std::lock_guard<std::mutex> l(mu);
			sampleAt = steadyMs();
			sampleVal = d;
		}

		void Progress::add(int64_t n)
		{
			int64_t d = done += n;
			std::lock_guard<std::mutex> l(mu);
			double now = steadyMs(), dt = now - sampleAt;
			if (dt < 500) return;
			// an exponential average: one slow piece doesn't make the time left jump about
			double inst = (d - sampleVal) * 1000.0 / dt, old = (double)speed;
			speed = (int64_t)(old > 0 ? 0.7 * old + 0.3 * inst : inst);
			sampleAt = now;
			sampleVal = d;
		}

		void Progress::retry(const std::string& why, int seconds, int a)
		{
			{
				std::lock_guard<std::mutex> l(mu);
				lastWhy = why;
			}
			attempt = a;
			speed = 0;
			retryAt = steadyMs() + seconds * 1000.0;
			state = RETRY;
		}

		std::string Progress::why()
		{
			std::lock_guard<std::mutex> l(mu);
			return lastWhy;
		}

		int64_t Progress::eta() const
		{
			int64_t t = total, d = done, s = speed;
			if (t < 0 || s <= 0 || d > t) return -1;
			return (t - d) / s;
		}

		// ---- the download ---------------------------------------------------------------------------------------------
		Result Downloader::download(const Item& it, Progress& p)
		{
			std::string part = it.local + PART, dir = dirName(it.local);
			if (!makeDirs(dir)) return Result(Err::LOCAL, "Can't make the folder " + dir + " on the card: " + strerror(errno));
			int64_t have = std::max<int64_t>(0, fileSize(part));
			if (it.size >= 0 && have > it.size) { unlink(part.c_str()); have = 0; }	// another file's part
			// the card first: before a byte comes, whether it fits (and FAT32's 4 GB)
			Space sp;
			if (it.size >= 0 && spaceAt(dir, sp))
			{
				if (sp.fat32 && (uint64_t)it.size > FAT32_MAX)
					return Result(Err::FAT32, "The file is " + bytes(it.size) + ", and this card (FAT32) takes 4 GB a file at most.");
				if ((int64_t)sp.free < it.size - have + (1 << 20))
					return Result(Err::NO_SPACE, "Not enough room on the card: " + bytes(it.size - have) + " needed, " + bytes((long long)sp.free) + " free.");
			}
			p.start(it.size, have);
			p.retries = retries;
			for (int attempt = 0;; attempt++)
			{
				int64_t before = std::max<int64_t>(0, fileSize(part));
				Result r = this->attempt(it, part, p);
				if (r) break;
				if (fileSize(part) > before) attempt = 0;	// the connection worked again: a new streak
				if (stopped()) return Result(Err::STOPPED, "Stopped.");
				if (r.err == Err::NO_RESUME && before > 0)
				{
					// the server won't start in the middle: from the beginning
					unlink(part.c_str());
					p.setDone(0);
					attempt--;
					continue;
				}
				if (!transient(r.err) || attempt >= retries) return r;
				be.reset();
				int wait = delays[std::min<size_t>((size_t)attempt, delays.size() - 1)];
				p.retry(r.msg, wait, attempt + 1);
				for (double until = steadyMs() + wait * 1000.0; steadyMs() < until;)
				{
					if (stopped()) return Result(Err::STOPPED, "Stopped.");
					std::this_thread::sleep_for(std::chrono::milliseconds(200));
				}
			}
			p.state = Progress::FINISHING;
			int64_t got = fileSize(part);
			if (it.size >= 0 && got != it.size)
				return Result(Err::OTHER, "The file came out " + std::to_string(got) + " bytes, not " + std::to_string(it.size) + ".");
			if (rename(part.c_str(), it.local.c_str()) != 0) return Result(Err::LOCAL, "Can't rename " + part + ": " + strerror(errno));
			p.state = Progress::DONE;
			return Result();
		}

		Result Downloader::attempt(const Item& it, const std::string& part, Progress& p)
		{
			p.state = Progress::CONNECTING;
			if (!be)
			{
				Result r;
				be = connect(r);
				if (!be) return r;
			}
			int64_t offset = std::max<int64_t>(0, fileSize(part));
			if (it.size >= 0 && offset == it.size && offset > 0) return Result();	// complete (cut off right before the rename)
			p.setDone(offset);
			int fd = open(part.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
			if (fd < 0) return Result(Err::LOCAL, "Can't write " + part + ": " + strerror(errno));

			// the watchdog: no byte for stallSeconds, or a stop, and the backend is aborted (closing its connection is the
			// one sure way to unblock a read on a dead Wi-Fi link)
			std::atomic<int64_t> counter{ 0 };
			std::atomic<bool> finished{ false }, stalled{ false };
			Backend* b = be.get();
			std::thread watchdog([&]
			{
				int64_t last = 0;
				double since = steadyMs();
				while (!finished)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(200));
					if (stopped()) { b->abort(); return; }
					int64_t v = counter;
					if (v != last) { last = v; since = steadyMs(); continue; }
					if (steadyMs() - since >= stallSeconds * 1000.0) { stalled = true; b->abort(); return; }
				}
			});
			int writeErr = 0;
			int64_t n = 0, unsynced = 0;
			p.state = Progress::DOWNLOADING;
			Result r = be->fetch(it.remote, offset, [&](const char* data, size_t len)
			{
				if (stopped()) return false;
				for (size_t w = 0; w < len;)
				{
					ssize_t k = write(fd, data + w, len - w);
					if (k < 0 && errno == EINTR) continue;
					if (k <= 0) { writeErr = k < 0 ? errno : ENOSPC; return false; }
					w += (size_t)k;
				}
				n += (int64_t)len;
				counter += (int64_t)len;
				p.add((int64_t)len);
				// what arrived is kept for resuming: on the card every 64 MB, not after every piece
				if ((unsynced += (int64_t)len) >= (64 << 20)) { fdatasync(fd); unsynced = 0; }
				if (rate > 0) std::this_thread::sleep_for(std::chrono::microseconds((int64_t)len * 1000000 / rate));
				return true;
			});
			finished = true;
			watchdog.join();
			int syncErr = fdatasync(fd) != 0 ? errno : 0;
			::close(fd);
			if (writeErr) return Result(writeErr == ENOSPC ? Err::NO_SPACE : Err::LOCAL, std::string("Writing to the card: ") + strerror(writeErr));
			if (stalled) return Result(Err::NETWORK, "No data came for " + std::to_string(stallSeconds) + " s: the connection seems dead.");
			if (stopped()) return Result(Err::STOPPED, "Stopped.");
			if (!r) return r;
			if (syncErr) return Result(Err::LOCAL, std::string("Writing to the card: ") + strerror(syncErr));
			if (it.size >= 0 && offset + n < it.size) return Result(Err::NETWORK, "The connection closed before the file was complete.");
			return Result();
		}
	}
}
