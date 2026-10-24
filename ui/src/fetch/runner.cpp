#include "runner.h"
#include "archive.h"
#include "jobfile.h"
#include "names.h"
#include "servers.h"
#include "transfer.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <set>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>

namespace banana
{
	namespace fetch
	{
		void logLine(const Paths& p, const std::string& s)
		{
			makeDirs(dirName(p.log()));
			char t[32];
			time_t now = time(nullptr);
			struct tm tm;
			localtime_r(&now, &tm);
			strftime(t, sizeof t, "%Y-%m-%d %H:%M:%S", &tm);
			std::ofstream(p.log(), std::ios::app) << t << " " << s << "\n";
		}

		namespace
		{
			std::atomic<bool> stopping{ false };
			void onStop(int) { stopping = true; }

			// the status line, written beside and renamed over: at most twice a second, unless asked to say it now
			class StatusFile
			{
			public:
				explicit StatusFile(const std::string& p) : path(p) {}
				void write(const Status& s, bool now)
				{
					std::string line = formatStatus(s);
					std::lock_guard<std::mutex> l(mu);
					double t = steadyMs();
					if (line == last || (!now && t - at < 500)) return;
					last = line;
					at = t;
					std::string tmp = path + ".new";
					if (FILE* f = fopen(tmp.c_str(), "w"))
					{
						fprintf(f, "%s\n", line.c_str());
						fclose(f);
						rename(tmp.c_str(), path.c_str());
					}
				}
			private:
				std::string path, last;
				std::mutex mu;
				double at = 0;
			};

			int readPid(const std::string& path)
			{
				std::ifstream f(path);
				int v = 0;
				f >> v;
				return v;
			}

			// transfer.pid: false while another runner holds it
			bool takeLock(const Paths& p)
			{
				int other = readPid(p.pid());
				if (other > 0 && other != getpid() && kill(other, 0) == 0) return false;
				std::string tmp = p.pid() + "." + std::to_string(getpid());
				{
					std::ofstream f(tmp);
					f << getpid() << "\n";
				}
				if (rename(tmp.c_str(), p.pid().c_str()) != 0) { unlink(tmp.c_str()); return false; }
				// (two started at the same moment: the one whose pid stayed carries on)
				usleep(50000);
				return readPid(p.pid()) == getpid();
			}

			void dropLock(const Paths& p)
			{
				if (readPid(p.pid()) == getpid()) unlink(p.pid().c_str());
			}

			bool readSecret(const std::string& path, std::string& out)
			{
				std::ifstream f(path, std::ios::binary);
				if (!f) return false;
				out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
				while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
				return true;
			}

			enum class End { FINISHED, STOPPED, WAITING };

			class JobRun
			{
			public:
				JobRun(const Paths& paths, Job& job, StatusFile& status) : p(paths), j(job), sf(status)
				{
					outs = readOutcomes(j.path + ".done");
					st.n = (int)j.items.size();
				}

				// done: what the job came to, to say once it is .finished
				End run(Status& done)
				{
					if (!prepareServer()) return End::WAITING;
					Downloader dl;
					dl.stop = &stopping;
					// (the tests: tries again at once, a quiet link given up on after 2 s; a download slow enough to cut)
					const char* t = getenv("BANANA_FETCH_TEST");
					const char* fast = getenv("BANANA_FETCH_TEST_FAST");
					const char* rate = getenv("BANANA_FETCH_TEST_RATE");
					if (t && !strcmp(t, "1") && fast && !strcmp(fast, "1")) { dl.delays = { 0 }; dl.stallSeconds = 2; }
					if (t && !strcmp(t, "1") && rate && *rate) dl.rate = atoll(rate);
					dl.connect = [&](Result& r) -> std::unique_ptr<Backend>
					{
						std::unique_ptr<Backend> b = makeBackend(srv);
						if (!b) { r = Result(Err::UNSUPPORTED, "This build of Banana can't talk " + srv.protocol + "."); return nullptr; }
						r = b->connect();
						if (!r) return nullptr;
						pin(b->fingerprint());
						return b;
					};
					Progress prog;
					std::string archivePassword;
					if (!j.archivePasswordFile.empty() && readSecret(j.archivePasswordFile, archivePassword)) unlink(j.archivePasswordFile.c_str());
					for (int i = 0; i < (int)j.items.size(); i++)
					{
						if (stopping) return End::STOPPED;
						const JobItem& it = j.items[i];
						const Outcome* o = outcome(i);
						if (it.extract)
						{
							if (o) continue;
							st.i = i + 1;
							Result r = unpack(it.remote, it.local, archivePassword);
							if (r.err == Err::STOPPED || stopping) return End::STOPPED;
							unpacked(i, it.remote, r);
							continue;
						}
						if (!o)
						{
							Result r = !blocked.empty() ? Result(Err::OTHER, blocked) : download(i, dl, prog);
							if (r.err == Err::STOPPED || stopping) return End::STOPPED;
							if (!r)
							{
								// a login refused, a server whose key changed: every file would fail the same way, so not tried
								if (r.err == Err::AUTH || r.err == Err::HOSTKEY) blocked = r.msg;
								record(i, "failed", std::string(errWord(r.err)) + ": " + r.msg);
								logLine(p, "failed " + it.remote + ": " + r.msg);
								continue;
							}
							record(i, "got", std::to_string(fileSize(it.local)));
							logLine(p, "downloaded " + it.remote + " to " + it.local + " (" + bytes(fileSize(it.local)) + ")");
							o = outcome(i);
						}
						// the last volume of an archive in: unpacked into its folder (but not a zip, when the folder's system
						// plays its games zipped and nobody asked for its zips unpacked)
						std::string first;
						if (j.unpack && o && o->what == "got" && lastVolume(i, first) && !(j.keepZips && kindOf(baseName(first)) == ZIP))
						{
							st.i = i + 1;
							Result r = unpack(first, dirName(first), "");
							if (r.err == Err::STOPPED || stopping) return End::STOPPED;
							unpacked(i, first, r);
						}
					}
					dl.close();
					done = summary(j, outs);
					return End::FINISHED;
				}

			private:
				const Paths& p;
				Job& j;
				StatusFile& sf;
				std::vector<Outcome> outs;
				Status st;
				ServerRec srv;
				std::string blocked;	// why every download of this job fails (the server gone, the login refused...)

				const Outcome* outcome(int i) const
				{
					for (auto& o : outs) if (o.index == i) return &o;
					return nullptr;
				}

				void record(int i, const std::string& what, const std::string& detail)
				{
					Outcome o;
					o.index = i;
					o.what = what;
					o.detail = detail;
					addOutcome(j.path + ".done", o);
					for (auto& x : outs) if (x.index == i) { x = o; return; }
					outs.push_back(o);
				}

				// the server and its password, for the downloads still to do: false when nobody remembered the password
				// (Downloads asks for it and runs the job again)
				bool prepareServer()
				{
					bool gets = false;
					for (int i = 0; i < (int)j.items.size(); i++) if (!j.items[i].extract && !outcome(i)) gets = true;
					if (!gets) return true;
					std::vector<ServerRec> list = loadServers(p.servers());
					ServerRec* s = findServer(list, j.server);
					if (!s) { blocked = "The server isn't in the list any more."; return true; }
					srv = *s;
					// (an empty password is one too: an account that has none)
					std::string pw, file = p.password(srv.id);
					bool have = readSecret(file, pw);
					if (have) unlink(file.c_str());
					else if (!srv.sealed.empty())
					{
						SecretBox box;
						have = box.open(p.key()) && box.unseal(srv.sealed, srv, pw);
					}
					if (!srv.guest && !have)
					{
						Status w;
						w.state = "need";
						w.n = (int)j.items.size();
						w.name = srv.title();
						w.why = "password";
						sf.write(w, true);
						logLine(p, "waiting for the password of " + srv.title());
						return false;
					}
					srv.password = pw;
					return true;
				}

				// the first connection to a server pins its key, as the browser does
				void pin(const std::string& fp)
				{
					if (fp.empty() || !srv.hostkey.empty()) return;
					srv.hostkey = fp;
					updateServers(p.servers(), [&](std::vector<ServerRec>& list)
					{
						ServerRec* s = findServer(list, srv.id);
						if (!s || !s->hostkey.empty()) return false;
						s->hostkey = fp;
						return true;
					});
				}

				Result download(int i, Downloader& dl, Progress& prog)
				{
					const JobItem& it = j.items[i];
					Status base = st;
					base.i = i + 1;
					base.name = baseName(it.local);
					base.why.clear();
					base.state = "connect";
					sf.write(base, true);
					std::atomic<bool> on{ true };
					std::thread ticker([&]
					{
						while (on)
						{
							Status s = base;
							int state = prog.state;
							s.state = state == Progress::RETRY ? "retry" : state == Progress::CONNECTING ? "connect" : "get";
							s.done = prog.done;
							s.total = prog.total;
							s.speed = prog.speed;
							s.eta = prog.eta();
							s.attempt = prog.attempt;
							s.retries = prog.retries;
							if (state == Progress::RETRY)
							{
								s.seconds = std::max(0, (int)((prog.retryAt - steadyMs()) / 1000 + 0.999));
								s.why = prog.why();
							}
							sf.write(s, false);
							std::this_thread::sleep_for(std::chrono::milliseconds(250));
						}
					});
					Item x;
					x.remote = it.remote;
					x.local = it.local;
					x.size = it.size;
					Result r = dl.download(x, prog);
					on = false;
					ticker.join();
					return r;
				}

				Result unpack(const std::string& archive, const std::string& dst, const std::string& password)
				{
					Status s = st;
					s.state = "unpack";
					s.percent = 0;
					s.name = baseName(archive);
					s.why.clear();
					sf.write(s, true);
					ArchiveOptions o;
					o.password = password;
					o.unwrap = j.unwrap;
					o.tempRoot = j.tempRoot;
					o.ignoreMemory = j.ignoreMemory;
					o.stop = &stopping;
					o.progress = [&](double f) { s.percent = (int)(f * 100); sf.write(s, false); };
					return extractArchive(archive, dst, o);
				}

				void unpacked(int i, const std::string& archive, const Result& r)
				{
					if (r)
					{
						record(i, "unpacked", baseName(archive));
						logLine(p, "unpacked " + archive);
						if (j.deleteArchive)
							for (auto& v : volumes(archive)) unlink(v.c_str());
						return;
					}
					record(i, "packed", std::string(errWord(r.err)) + ": " + r.msg);
					logLine(p, "not unpacked " + archive + ": " + r.msg);
				}

				// the item completes an archive (a plain one, or the last volume of a set to come in): its first volume
				bool lastVolume(int i, std::string& first)
				{
					std::string key, f2, k2;
					if (!volumeOf(j.items[i].local, first, key)) return false;
					for (size_t k = (size_t)i + 1; k < j.items.size(); k++)
						if (!j.items[k].extract && volumeOf(j.items[k].local, f2, k2) && k2 == key) return false;
					return fileSize(first) >= 0;
				}
			};

			// the job's file and its outcomes as a finished job's: its new path
			std::string finish(const std::string& job)
			{
				std::string base = job.substr(0, job.size() - 4);	// (".job")
				rename(job.c_str(), (base + ".finished").c_str());
				rename((job + ".done").c_str(), (base + ".finished.done").c_str());
				return base + ".finished";
			}

			// a job done, told to the menu: its watch on /run/banana wakes for this file (not for the status, in a folder
			// of its own), and its pop-up counts the finished jobs it hasn't seen
			void announce(const Paths& p, const std::string& finished)
			{
				std::string tmp = p.finished() + ".new";
				if (FILE* f = fopen(tmp.c_str(), "w"))
				{
					fprintf(f, "%s\n", baseName(finished).c_str());
					fclose(f);
					rename(tmp.c_str(), p.finished().c_str());
				}
			}

			// the last few finished jobs stay, for Downloads to show what happened
			void prune(const std::string& dir)
			{
				std::vector<std::string> done = finishedJobs(dir);
				for (size_t i = 5; i < done.size(); i++)
				{
					unlink(done[i].c_str());
					unlink((done[i] + ".done").c_str());
				}
			}
		}

		int runJobs(const Paths& p)
		{
			makeDirs(dirName(p.status()));
			makeDirs(p.jobs());
			if (!takeLock(p)) return 0;
			struct sigaction sa = {};
			sa.sa_handler = onStop;
			sigaction(SIGTERM, &sa, nullptr);
			sigaction(SIGINT, &sa, nullptr);
			sigaction(SIGHUP, &sa, nullptr);
			// beside the menu and a game: the CPU's leftovers, and the card when nothing else wants it
			setpriority(PRIO_PROCESS, 0, 10);
			syscall(SYS_ioprio_set, 1 /* IOPRIO_WHO_PROCESS */, 0, 3 << 13 /* IOPRIO_CLASS_IDLE */);
			StatusFile sf(p.status());
			std::set<std::string> cleaned;
			for (;;)
			{
				bool dry = false;	// the queue ran out (not stopped, nor waiting for a password)
				for (;;)
				{
					if (stopping) break;
					std::vector<std::string> jobs = pendingJobs(p.jobs());
					if (jobs.empty()) { dry = true; break; }
					Job j;
					if (!readJob(jobs[0], j)) { rename(jobs[0].c_str(), (jobs[0] + ".broken").c_str()); continue; }
					// what an unpacking cut short by a power cut left (no other runner is unpacking now)
					if (!j.tempRoot.empty() && cleaned.insert(j.tempRoot).second) removeStaleTemp(j.tempRoot);
					logLine(p, "job " + j.title);
					Status done;
					End e = JobRun(p, j, sf).run(done);
					if (e == End::STOPPED)
					{
						Status s;
						s.state = "stopped";
						s.name = j.title;
						sf.write(s, true);
						logLine(p, "stopped");
						break;
					}
					if (e == End::WAITING) break;
					std::string finished = finish(jobs[0]);
					prune(p.jobs());
					// (said once the job is .finished: that is what the menu's pop-up counts)
					sf.write(done, true);
					announce(p, finished);
				}
				// (the tests: the queue's last look and the lock let go further apart, as a long sync() after an unpacking
				// makes them)
				const char* t = getenv("BANANA_FETCH_TEST");
				const char* linger = getenv("BANANA_FETCH_TEST_LINGER");
				if (dry && t && !strcmp(t, "1") && linger && *linger) usleep((useconds_t)atoi(linger) * 1000);
				sync();
				dropLock(p);
				// a job written between the last look and the lock let go: the menu saw a runner then and started none,
				// so this one goes on with it (unless another runner has started meanwhile)
				if (!dry || stopping || pendingJobs(p.jobs()).empty() || !takeLock(p)) break;
			}
			return 0;
		}
	}
}
