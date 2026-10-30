// banana-ui: Files' servers. A NAS's or a computer's folders over SMB, SFTP or FTP, downloaded onto the cards, and
// archives on the cards unpacked (as the settings' pages: a list on the bottom panel, what is chosen explained on the
// top one). Files' own pages (settings_files.cpp) list the servers beside the cards, and choose a folder by their usual
// navigation; these are the rest:
//   browse    a server's folder: A opens (a file: what can be done), Y marks, X what to do, B up
//   actions   Download (its page), Copy (Files' clipboard: Paste here in a card's folder downloads there), Mark all
//   target    where a download (or an unpacking) goes: the folder (guessed, or any chosen in Files), the card, the
//             unpacking; the files are listed, folders gone through, and the job written
//   status    the job: its files, the one in hand's progress; Stop, Resume, Cancel
//   edit      a server: its protocol, address, login, start folder...
//   options   unpacking after a download, the archive deleted after, one folder unwrapped, hidden files on the servers
// As in Files, LEFT and RIGHT turn a page (on a choice's row, its value), L and R go to the first and last rows.
// banana-fetch does the network and the unpacking: its session (nas::Session) logs in and lists while the pages need it,
// its runner does the jobs in the background, also while the menu does anything else. This file asks and draws.
#include "settings.h"
#include "settings_defaults.h"
#include "settings_draw.h"
#include "fileops.h"
#include "nas.h"
#include "sysfolders.h"
#include "fetch/names.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

using rnds::Color;

namespace banana
{
	namespace
	{
		double steady()
		{
			return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		std::string firstLine(const std::string& path)
		{
			std::ifstream f(path);
			std::string s;
			std::getline(f, s);
			return s;
		}

		bool isFile(const std::string& p)
		{
			struct stat st;
			return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
		}

		bool isDir(const std::string& p)
		{
			struct stat st;
			return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
		}

		int64_t sizeOf(const std::string& p)
		{
			struct stat st;
			return stat(p.c_str(), &st) == 0 ? (int64_t)st.st_size : 0;
		}

		std::string upper(std::string v)
		{
			for (auto& c : v) c = (char)toupper((unsigned char)c);
			return v;
		}

		std::string protoLabel(const fetch::ServerRec& s)
		{
			return s.protocol == "ftp" && s.tls ? "FTPS" : upper(s.protocol);
		}

		std::string address(const fetch::ServerRec& s)
		{
			std::string p = s.protocol == "ftp" && s.tls ? "ftps" : s.protocol;
			std::string a = p + "://" + (s.guest ? "" : s.user.empty() ? "" : s.user + "@") + s.host;
			if (s.port > 0 && s.port != s.defaultPort()) a += ":" + std::to_string(s.port);
			return a + (s.start.empty() ? "" : s.start);
		}

		std::string plural(long long n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

		std::string duration(int64_t s)
		{
			if (s < 60) return std::to_string(s) + " s";
			if (s < 3600) return std::to_string(s / 60) + " min";
			return std::to_string(s / 3600) + " h " + std::to_string(s / 60 % 60) + " min";
		}

		// a file's date for its row
		std::string date(int64_t t)
		{
			if (t <= 0) return "";
			char b[32];
			time_t tt = (time_t)t;
			struct tm tm;
			localtime_r(&tt, &tm);
			strftime(b, sizeof b, "%d %b %Y", &tm);
			return b;
		}
	}

	struct Settings::Servers
	{
		enum Sub { BROWSE, ACTIONS, TARGET, STATUS, EDIT, OPTIONS, SUBS };
		enum Ask { NOTHING, CONNECT, LIST, WALK, INSPECT, PUT, FORGET, TRUST, REMEMBER, RECONNECT };
		enum Field { F_NONE, F_NAME, F_HOST, F_PORT, F_USER, F_PASSWORD, F_DOMAIN, F_START, F_LOGIN, F_STATUS, F_ARCHIVE };
		// what each row does
		enum Kind { B_ITEM, A_DOWNLOAD, A_COPY, A_MARKALL, A_UNMARKALL,
			E_NAME, E_PROTOCOL, E_HOST, E_PORT, E_GUEST, E_USER, E_PASSWORD, E_REMEMBER, E_DOMAIN, E_START, E_TLS, E_ENCODING, E_SAVE, E_DELETE,
			T_FOLDER, T_CARD, T_UNPACK, T_ZIPS, T_DELETE, T_EXISTING, T_GO, S_ITEM, S_CANCEL, O_UNPACK, O_DELETE, O_UNWRAP, O_HIDDEN, K_TRUST };
		// where a page was opened from, which leaving it goes back to
		enum From { FROM_ROOT, FROM_SERVER, FROM_FILES };
		// how the target folder came: guessed from the files, where this server's folder went last, the roms folder (no
		// guess), chosen in Files, the folder pasted into (or the archive's own)
		enum How { BY_TYPE, LAST, ROMS, CHOSEN, HERE };
		// files of a download already on the card
		enum Existing { SKIP, REPLACE, KEEP_BOTH };

		struct Row
		{
			std::string label, value;
			int kind = 0, index = -1;
			bool choice = false;	// ◀ value ▶
			bool mark = false;		// a tick before the label
			bool warn = false;		// the value in the accent
		};

		struct Item { std::string name; int64_t size = 0, mtime = 0; bool dir = false, link = false; };
		struct File { int64_t size; std::string remote, local; };

		Settings* ui;
		bool sync = false;
		std::string prog;
		fetch::Paths paths;
		nas::Session session;
		Sub sub = BROWSE;
		Ask asked = NOTHING;		// what the session is answering
		Ask queuedWhat = NOTHING;	// a request made meanwhile (behind a remembered folder's), sent when that answer is in
		std::vector<std::string> queued;
		int sel[SUBS] = {}, top[SUBS] = {};
		std::string message;		// the top panel's word: what went wrong, what happened
		int armed = -1;				// an action waiting for its second press (its kind)
		Field field = F_NONE;		// what the keyboard is typing

		std::vector<fetch::ServerRec> servers;
		std::map<std::string, std::string> typed;	// passwords typed this time, by server (the ones not remembered)
		// the server in hand, and the one the session is logged in to
		fetch::ServerRec srv;
		std::string home, keySeen, connectedId;	// keySeen: the key it shows now, when that isn't the one pinned (A trusts it)
		// browse
		std::string dir, listing;	// the folder shown; the one being listed
		bool up = false;			// listing is the folder above, for B: when it doesn't come, Files' list
		bool listed = false;		// a folder of this server has been shown since it was opened
		std::vector<Item> items;
		std::set<std::string> marked;
		std::map<std::string, std::pair<int, int>> cursor;	// a folder's selection and scroll, to come back to
		std::string linkTried;		// a link opened as a folder: if it doesn't list, it is a file
		std::vector<Item> targets;	// what the actions are for
		// target
		std::vector<files::Card> cards;
		std::vector<sysfolders::Folder> folders;
		std::string dest;			// the folder it goes into (a card's, any)
		How how = ROMS;
		bool unpack = true, del = false;
		Existing existing = SKIP;
		bool askUnpack = false, askDelete = false, existingAsked = false;
		bool askZips = false, unpackZips = false;	// the zips' row (a system that plays them zipped, other archives too)
		bool local = false;			// unpacking archives on a card (not a download)
		From origin = FROM_SERVER;	// the target page's: back there with B
		std::string originDir;		// (FROM_FILES: the folder in Files)
		std::string chosenDir;		// the server's folder the chosen entries are in
		std::vector<Item> chosen;
		std::vector<File> files;
		bool walked = false, inspected = false;
		int existingCount = 0;
		std::vector<std::string> archives;	// unpacking: the archives (their first volumes)
		std::string archivePassword;
		bool ignoreMemory = false;
		// status
		std::string statusLine, jobPath;
		fetch::Status st;
		fetch::Job job;
		std::vector<fetch::Outcome> outcomes;
		bool finished = false;
		int jobsBehind = 0;			// pending jobs after the one shown
		double stNext = 0;
		std::string cancelling;		// a job cancelled, deleted once its runner is gone
		bool restartAfter = false;	// the runner was at work then: it goes on with the jobs after it
		From statusFrom = FROM_ROOT;
		std::string statusDir;		// (FROM_FILES: the folder in Files)
		// edit
		fetch::ServerRec draft;
		std::string draftPassword, putPassword;
		bool passwordTyped = false, putTyped = false, existingServer = false, portTouched = false;

		// ---- the basics ---------------------------------------------------------------------------------------------
		void log(const std::string& m) { if (ui->log) ui->log("servers: " + m); }

		std::string option(const char* key, const char* def)
		{
			std::string d = settingDefault(key);
			return getConfValue(paths.data + "/settings.conf", key, d.empty() ? def : d);
		}

		fetch::ServerRec* serverById(const std::string& id)
		{
			for (auto& s : servers) if (s.id == id) return &s;
			return nullptr;
		}

		void loadServers()
		{
			servers = fetch::loadServers(paths.servers());
			std::stable_sort(servers.begin(), servers.end(), [](const fetch::ServerRec& a, const fetch::ServerRec& b)
			{
				return fetch::lower(a.title()) < fetch::lower(b.title());
			});
		}

		// what every way in needs: the cards as they are now, systems.conf's folders, the helper's answers waking the loop
		void prepare()
		{
			cards = files::cards(ui->romRoot);
			folders = sysfolders::folders(ui->systemsConf);
			session.onAnswer = ui->wakeHost;	// (the loop woken when an answer is in, rather than looking every 50 ms)
			armed = -1;
		}

		void close()
		{
			// (a cancel under way finished first: the runner it stopped ends within a moment, a second at most)
			for (int i = 0; i < 20 && !cancelling.empty() && !cancelled(); i++) usleep(50000);
			session.stop();
			connectedId.clear();
			asked = queuedWhat = NOTHING;
			queued.clear();
			up = false;
		}

		bool ask(Ask what, const std::vector<std::string>& req)
		{
			bool fresh = !session.alive();
			if (fresh) connectedId.clear();
			if (fresh && !session.start(prog, paths.data, sync)) { message = "Files' helper (" + prog + ") couldn't be started."; return false; }
			if (session.busy()) { queuedWhat = what; queued = req; return true; }
			// logged in to the server in hand first: a new helper (the last one went after its idle minutes), or one logged
			// in to another server since (a paste of a server's files, browsed before another one)
			if ((what == LIST || what == WALK) && !srv.id.empty() && connectedId != srv.id)
			{
				std::vector<std::string> c = { "connect", srv.id };
				auto t = typed.find(srv.id);
				if (t != typed.end()) c.push_back(t->second);
				if (!session.ask(c)) return false;
				asked = RECONNECT;
				queuedWhat = what;
				queued = req;
				return true;
			}
			if (!session.ask(req)) return false;
			asked = what;
			return true;
		}

		// an answer the pages wait for (a remembered folder's isn't one)
		bool waiting() const { return (asked != NOTHING && asked != REMEMBER) || queuedWhat != NOTHING; }

		// B while a server is slow: the helper stopped (the next request starts it again)
		void giveUp()
		{
			session.stop();
			connectedId.clear();
			asked = queuedWhat = NOTHING;
			queued.clear();
			up = false;
			message = "Stopped.";
		}

		void go(Sub to)
		{
			sub = to;
			armed = -1;
		}

		void keyboard(Field f, const std::string& title, const std::string& what, const std::string& start, const std::string& help)
		{
			field = f;
			message.clear();
			ui->startEntry(ENTRY_SERVER, title, what, SERVERS, start);
			ui->entryHelp = help + " START when it is typed.";
		}

		// ---- the cards ------------------------------------------------------------------------------------------------
		const files::Card* cardOf(const std::string& path) const
		{
			const files::Card* best = nullptr;
			for (auto& c : cards) if (files::inside(path, c.root) && (!best || c.root.size() > best->root.size())) best = &c;
			return best;
		}

		// a folder from its card's root ("roms/gba"; "" the root itself)
		std::string relOf(const std::string& path) const
		{
			const files::Card* c = cardOf(path);
			if (!c || path.size() <= c->root.size()) return "";
			return path.substr(c->root.size() + 1);
		}

		// a folder as the pages say it: the card's name, then the path on it
		std::string where(const std::string& path) const
		{
			const files::Card* c = cardOf(path);
			if (!c) return path;
			std::string rel = relOf(path);
			return rel.empty() ? c->label : c->label + " \xe2\x80\xba " + rel;
		}

		// Banana's own folder on the data card, banana/ (the servers, the settings, the jobs): nothing goes there (nor will
		// banana-fetch write there)
		bool inBanana(const std::string& path) const { return files::inside(path, ui->data); }

		// the folder chosen still on a card, not Banana's own: "" when it can be, else why not
		std::string destProblem() const
		{
			if (!cardOf(dest)) return "That card was taken out: choose the folder again.";
			if (inBanana(dest)) return "Not into banana/: it holds Banana's own files (the settings, the servers, the jobs).";
			return "";
		}

		// ---- the job and its status ----------------------------------------------------------------------------------
		// the job to show: the one being done (or waiting), else the last finished
		bool pollStatus(bool now)
		{
			double t = steady();
			if (!now && t < stNext) return false;
			stNext = t + 500;
			std::string before = statusLine + "|" + jobPath + "|" + std::to_string(outcomes.size()) + "|" + std::to_string(finished);
			std::vector<std::string> pending = fetch::pendingJobs(paths.jobs()), done = fetch::finishedJobs(paths.jobs());
			jobPath = !pending.empty() ? pending[0] : !done.empty() ? done[0] : "";
			finished = pending.empty() && !jobPath.empty();
			jobsBehind = pending.empty() ? 0 : (int)pending.size() - 1;
			if (jobPath.empty()) job = fetch::Job();
			else if (job.path != jobPath) fetch::readJob(jobPath, job);
			outcomes = jobPath.empty() ? std::vector<fetch::Outcome>() : fetch::readOutcomes(jobPath + ".done");
			statusLine = firstLine(paths.status());
			st = fetch::parseStatus(statusLine);
			return before != statusLine + "|" + jobPath + "|" + std::to_string(outcomes.size()) + "|" + std::to_string(finished);
		}

		bool running() { return nas::runner(paths) != 0; }

		// a job not finished, running or not, can be given up on
		bool canCancel() const { return !finished && !jobPath.empty() && cancelling.empty(); }

		const fetch::Outcome* outcome(int i) const
		{
			for (auto& o : outcomes) if (o.index == i) return &o;
			return nullptr;
		}

		bool isUnpack() const { return job.server.empty() && !job.items.empty(); }

		// the job in a few words, for Files' row and the status page
		std::string jobState(bool brief)
		{
			if (jobPath.empty()) return "";
			if (finished)
			{
				fetch::Status d = fetch::summary(job, outcomes);
				std::string r = brief ? "Done" : "Finished: " + std::to_string(d.downloaded) + " downloaded";
				if (!brief && isUnpack()) r = "Finished";
				if (!brief && d.unpacked) r += ", " + std::to_string(d.unpacked) + " unpacked";
				if (d.failed) r += ", " + std::to_string(d.failed) + " failed";
				if (!brief && d.packed) r += ", " + std::to_string(d.packed) + " left packed";
				return r;
			}
			if (!cancelling.empty()) return brief ? "Cancelling" : "Cancelling: the job stops";
			if (st.state == "need") return brief ? "Needs a password" : "Waiting for the password of " + st.name;
			if (!running())
			{
				if (st.state == "stopped" && !st.why.empty()) return brief ? "Stopped" : "Stopped: " + st.why + " A goes on once it is back.";
				return brief ? "Stopped" : "Stopped: A goes on from where it was";
			}
			std::string of = std::to_string(st.i) + " of " + std::to_string(st.n);
			if (st.state == "get")
			{
				int pc = st.total > 0 ? (int)(st.done * 100 / st.total) : 0;
				if (brief) return of + " \xc2\xb7 " + std::to_string(pc) + "%";
				std::string r = "Downloading " + of + ": " + std::to_string(pc) + "%";
				if (st.speed > 0) r += " \xc2\xb7 " + fetch::bytes(st.speed) + "/s";
				if (st.eta >= 0) r += " \xc2\xb7 " + duration(st.eta) + " left";
				return r;
			}
			if (st.state == "retry") return brief ? "Trying again" : "Trying again in " + std::to_string(st.seconds) + " s (" + std::to_string(st.attempt) + " of " + std::to_string(st.retries) + ")";
			if (st.state == "unpack") return brief ? "Unpacking " + std::to_string(st.percent) + "%" : "Unpacking " + of + ": " + std::to_string(st.percent) + "%";
			if (st.state == "connect") return brief ? "Connecting" : "Connecting (" + of + ")";
			return brief ? "Working" : "Starting";
		}

		void resume()
		{
			// a password not remembered goes to the runner again (it reads its file once): typed this time already, or now
			auto t = job.server.empty() ? typed.end() : typed.find(job.server);
			if (t != typed.end()) nas::writeSecret(paths.password(job.server), t->second);
			else if (st.state == "need" && !job.server.empty())
			{
				std::string name = st.name.empty() ? "the server" : st.name;
				keyboard(F_STATUS, "Password for", name, "", "The password of " + name + ": the download goes on with it. It isn't kept (the server's Remember password is off).");
				return;
			}
			nas::startRunner(prog, paths.data, sync);
			message.clear();
			pollStatus(true);
		}

		void stop()
		{
			nas::stopRunner(paths);
			message = "Stopped. The files downloaded so far are kept: A goes on from where it was.";
		}

		// Cancel: the job's runner stopped first (it keeps the parts until it is gone), then the job deleted, with what its
		// unfinished downloads left on the card. The files it finished stay, and the jobs after it go on.
		void cancel()
		{
			cancelling = jobPath;
			restartAfter = running();
			nas::stopRunner(paths);
			message = "Cancelling...";
			cancelled();
		}

		// (true once the runner is gone and the job with it)
		bool cancelled()
		{
			if (cancelling.empty() || running()) return false;
			nas::dropJob(cancelling);
			log(cancelling + " cancelled");
			cancelling.clear();
			message = "Cancelled. The files it had finished stay on the card.";
			bool more = !fetch::pendingJobs(paths.jobs()).empty();
			if (restartAfter && more) nas::startRunner(prog, paths.data, sync);
			restartAfter = false;
			pollStatus(true);
			if (!more && ui->page == SERVERS && sub == STATUS) leaveStatus();
			return true;
		}

		// the job's page left (B, the job cleared or cancelled): back where it was opened, the folder a job was started in,
		// on the server or the card, as it was, or Files' list from the job's row there
		void leaveStatus()
		{
			message.clear();
			if (statusFrom == FROM_SERVER) { go(BROWSE); return; }
			if (statusFrom == FROM_FILES) { ui->filesShow(statusDir, "", false); return; }
			ui->filesRoot("job", "");
		}

		// ---- the rows -----------------------------------------------------------------------------------------------
		std::string what(const std::vector<Item>& v) const { return v.size() == 1 ? v[0].name : plural((long long)v.size(), "item", "items"); }

		std::vector<Row> rows()
		{
			std::vector<Row> r;
			auto add = [&](const std::string& label, const std::string& value, int kind, int index = -1)
			{
				Row x;
				x.label = label; x.value = value; x.kind = kind; x.index = index;
				r.push_back(x);
				return &r.back();
			};
			switch (sub)
			{
			case BROWSE:
				// (the words only for the rows on screen and the one chosen: a server's folder may hold thousands of names,
				// and its rows are made at every press and every frame)
				r.reserve(items.size());
				for (size_t i = 0; i < items.size(); i++)
				{
					const Item& it = items[i];
					int at = (int)i;
					bool shown = at == sel[BROWSE] || (at >= top[BROWSE] && at < top[BROWSE] + ROWS);
					Row* x = add(shown ? it.name : std::string(), !shown ? "" : it.dir ? "Folder" : it.link ? "Link" : it.size >= 0 ? fetch::bytes(it.size) : "", B_ITEM, at);
					x->mark = shown && marked.count(it.name) > 0;
				}
				break;
			case ACTIONS:
				add("Download", what(targets), A_DOWNLOAD);
				add("Copy", what(targets), A_COPY);
				if (!items.empty()) add(marked.size() < items.size() ? "Mark all" : "Unmark all", plural((long long)items.size(), "item", "items"),
					marked.size() < items.size() ? A_MARKALL : A_UNMARKALL);
				break;
			case TARGET:
			{
				std::string why = how == BY_TYPE ? " (by type)" : how == LAST ? " (as last time)" : "";
				add("Folder", where(dest) + (isDir(dest) || !cardOf(dest) ? "" : " (new)") + why, T_FOLDER);
				const files::Card* c = cardOf(dest);
				if (cards.size() > 1 && c)
				{
					fetch::Space sp;
					std::string free = fetch::spaceAt(c->root, sp) ? " \xc2\xb7 " + fetch::bytes((long long)sp.free) + " free" : "";
					add("Card", c->label + free, T_CARD)->choice = true;
				}
				if (askUnpack) add("Unpack archives", unpack ? "Yes" : "No", T_UNPACK)->choice = true;
				if (askZips && unpack) add("Zips", unpackZips ? "Unpack them too" : "Keep them as they are", T_ZIPS)->choice = true;
				if (askDelete && unpack) add(local ? "Delete the archive after" : "Delete archives after", del ? "Yes" : "No", T_DELETE)->choice = true;
				if (existingAsked) add("Already here", existing == SKIP ? "Skip them" : existing == REPLACE ? "Replace them" : "Keep both", T_EXISTING)->choice = true;
				add(local ? "Unpack" : "Download", "", T_GO);
				break;
			}
			case STATUS:
				for (size_t i = 0; i < job.items.size(); i++)
				{
					const fetch::JobItem& it = job.items[i];
					std::string name = it.extract ? fetch::baseName(it.remote) : fetch::baseName(it.local);
					const fetch::Outcome* o = outcome((int)i);
					std::string v = o ? (o->what == "got" ? "Done" : o->what == "unpacked" ? "Unpacked" : o->what == "packed" ? "Not unpacked" : "Failed")
						: !finished && (int)i + 1 == st.i && running() ? (st.state == "unpack" ? std::to_string(st.percent) + "%" :
							st.total > 0 ? std::to_string(st.done * 100 / st.total) + "%" : "...") : it.size > 0 ? fetch::bytes(it.size) : "";
					Row* x = add(name, v, S_ITEM, (int)i);
					x->mark = o && (o->what == "got" || o->what == "unpacked");
					x->warn = o && (o->what == "failed" || o->what == "packed");
				}
				break;
			case EDIT:
			{
				const fetch::ServerRec& d = draft;
				bool ftp = d.protocol == "ftp", smb = d.protocol == "smb", sftp = d.protocol == "sftp";
				add("Name", d.name.empty() ? "(the address)" : d.name, E_NAME);
				add("Protocol", ftp ? "FTP" : sftp ? "SFTP" : "SMB", E_PROTOCOL)->choice = true;
				add("Address", d.host.empty() ? "Type it" : d.host, E_HOST)->warn = d.host.empty();
				add("Port", std::to_string(d.port > 0 ? d.port : d.defaultPort()), E_PORT);
				if (!sftp) add(ftp ? "Anonymous" : "Guest", d.guest ? "Yes" : "No", E_GUEST)->choice = true;
				if (!d.guest || sftp)
				{
					add("User", d.user.empty() ? "Type it" : d.user, E_USER);
					// (an empty one typed is a password too: an account that has none)
					std::string pw = passwordTyped ? (draftPassword.empty() ? "Empty" : std::string(std::min<size_t>(draftPassword.size(), 12), '*'))
						: !d.sealed.empty() ? "Kept" : "None";
					add("Password", pw, E_PASSWORD);
					add("Remember password", d.remember ? "Yes" : "No", E_REMEMBER)->choice = true;
				}
				if (smb) add("Domain", d.domain.empty() ? "None" : d.domain, E_DOMAIN);
				add("Start folder", d.start.empty() ? "Where the login lands" : d.start, E_START);
				if (ftp)
				{
					add("FTPS (TLS)", d.tls ? "Yes" : "No", E_TLS)->choice = true;
					add("File names", upper(d.encoding.empty() ? "utf-8" : d.encoding), E_ENCODING)->choice = true;
				}
				Row* s = add("Save", "", E_SAVE);
				if (armed == E_SAVE) { s->value = "Press A again"; s->warn = true; }
				if (existingServer)
				{
					Row* x = add("Delete this server", "", E_DELETE);
					if (armed == E_DELETE) { x->value = "Press A again"; x->warn = true; }
				}
				break;
			}
			case OPTIONS:
			{
				auto three = [](const std::string& v) { return v == "always" ? "Always" : v == "never" ? "Never" : "Ask"; };
				add("Unpack after a download", three(option("files_unpack", "ask")), O_UNPACK)->choice = true;
				add("Delete the archive after", three(option("files_delete_archive", "ask")), O_DELETE)->choice = true;
				add("Unwrap a single folder", option("files_unwrap", "on") == "on" ? "On" : "Off", O_UNWRAP)->choice = true;
				add("Hidden files on servers", option("files_hidden", "off") == "on" ? "Shown" : "Hidden", O_HIDDEN)->choice = true;
				break;
			}
			default:
				break;
			}
			return r;
		}

		// ---- the server in hand -------------------------------------------------------------------------------------
		void connect(const fetch::ServerRec& s)
		{
			srv = s;
			keySeen.clear();
			std::vector<std::string> req = { "connect", s.id };
			auto t = typed.find(s.id);
			if (t != typed.end()) req.push_back(t->second);
			else if (!s.guest && (s.sealed.empty() || !s.remember)) { askLogin(); return; }
			if (ask(CONNECT, req)) message = "Connecting to " + s.title() + "...";
		}

		void askLogin()
		{
			std::string who = (srv.user.empty() ? "" : srv.user + "@") + srv.host;
			keyboard(F_LOGIN, "Password for", who, "", srv.remember ? "It is kept on the card, sealed for this handheld (Remember password)."
				: "It isn't kept: Files asks for it again next time (the server's Remember password is off).");
		}

		bool list(const std::string& path)
		{
			listing = fetch::cleanPath(path);
			up = false;
			if (!ask(LIST, { "list", listing })) return false;
			message = "Listing " + listing + "...";
			return true;
		}

		void answered(const std::vector<std::vector<std::string>>& rows, const std::vector<std::string>& last)
		{
			Ask what = asked;
			asked = NOTHING;
			bool ok = !last.empty() && last[0] != "err";
			std::string word = last.size() > 1 ? last[1] : "", text = last.size() > 2 ? last[2] : "";
			// (banana-ui's log: what was asked and what went wrong, never a password)
			static const char* const ASKED[] = { "-", "connect", "list", "walk", "inspect", "put", "delete", "trust", "target", "reconnect" };
			if (!ok) log(std::string(ASKED[what]) + ": " + word + ": " + text);
			switch (what)
			{
			case CONNECT:
				if (ok)
				{
					message.clear();
					connectedId = srv.id;
					home = last.size() > 2 && !last[2].empty() ? last[2] : "/";
					log("connected to " + srv.title() + " (" + protoLabel(srv) + "), home " + home);
					// (a paste's server logged in to: its files gone through now)
					if (sub == TARGET) { walk(); return; }
					cursor.clear();
					marked.clear();
					list(srv.start.empty() ? home : srv.start);
					return;
				}
				if (word == "password") { askLogin(); return; }
				if (word == "auth")
				{
					typed.erase(srv.id);
					askLogin();
					ui->message = "The server refused the name or the password: type it again.";
					return;
				}
				if (word == "hostkey" && last.size() > 4)
				{
					keySeen = last[4];
					// (one paragraph: the help text has no line breaks)
					message = srv.title() + "'s key changed: it isn't the server it was (" + last[3] + " before, " + last[4] +
						" now). If it wasn't reinstalled, someone may be in between. Press A again to trust the new key.";
					armed = K_TRUST;
					return;
				}
				message = text;
				return;
			case LIST:
				if (!ok)
				{
					if (!linkTried.empty())
					{
						// a link that doesn't list is a file: what can be done with it
						Item it;
						it.name = linkTried;
						it.size = -1;
						linkTried.clear();
						targets = { it };
						go(ACTIONS);
						sel[ACTIONS] = top[ACTIONS] = 0;
						return;
					}
					message = text;
					// the folder above (B) didn't come (the server unreachable, Wi-Fi off a while after a sleep, a folder
					// not allowed): Files' list, rather than this folder for good
					if (up) { up = false; ui->filesRoot("server:" + srv.id, message); return; }
					if (!listed)
					{
						// the start folder didn't open: the page on it, empty, B goes up from there
						dir = listing;
						items.clear();
						marked.clear();
						sel[BROWSE] = top[BROWSE] = 0;
						listed = true;
						go(BROWSE);
					}
					return;
				}
				up = false;
				{
					bool hidden = option("files_hidden", "off") == "on";
					std::string was = dir;
					if (listed && !was.empty()) cursor[was] = { sel[BROWSE], top[BROWSE] };
					dir = listing;
					linkTried.clear();
					items.clear();
					for (auto& f : rows)
					{
						if (f.size() < 5 || f[0] != "e") continue;
						if (!hidden && !f[4].empty() && f[4][0] == '.') continue;
						Item it;
						it.dir = f[1] == "d";
						it.link = f[1] == "l";
						it.size = atoll(f[2].c_str());
						it.mtime = atoll(f[3].c_str());
						it.name = f[4];
						items.push_back(it);
					}
					if (was != dir) marked.clear();
					log(dir + ": " + std::to_string(items.size()) + " entries");
					auto c = cursor.find(dir);
					sel[BROWSE] = c == cursor.end() ? 0 : std::min(c->second.first, std::max(0, (int)items.size() - 1));
					top[BROWSE] = c == cursor.end() ? 0 : std::min(c->second.second, sel[BROWSE]);
					message.clear();
					listed = true;
					if (sub != ACTIONS) go(BROWSE);
				}
				return;
			case WALK:
				if (!ok) { message = text; return; }
				files.clear();
				for (auto& f : rows)
					if (f.size() >= 4 && f[0] == "f") files.push_back({ atoll(f[1].c_str()), f[2], f[3] });
				walked = true;
				existingCount = 0;
				for (auto& f : files) if (isFile(f.local)) existingCount++;
				if (existingCount > 0 && !existingAsked)
				{
					existingAsked = true;
					message = plural(existingCount, "file", "files") + " of " + std::to_string(files.size()) + (existingCount == 1 ? " is" : " are") +
						" there already: skip them, replace them or keep both, then Download.";
					return;
				}
				download();
				return;
			case INSPECT:
				if (!ok)
				{
					if (word == "password" || word == "wrong_password")
					{
						keyboard(F_ARCHIVE, "Password for", fetch::baseName(archives[0]), "",
							word == "wrong_password" ? "That password is wrong: type it again." : "The archive is protected by a password.");
						if (word == "wrong_password") ui->message = "That password is wrong: type it again.";
						return;
					}
					message = text;
					return;
				}
				{
					// files, bytes, the largest, the dictionary, encrypted, the memory free
					int64_t dict = last.size() > 4 ? atoll(last[4].c_str()) : 0, mem = last.size() > 6 ? atoll(last[6].c_str()) : -1;
					inspected = true;
					message = plural(atoll(last[1].c_str()), "file", "files") + ", " + fetch::bytes(atoll(last[2].c_str())) + " unpacked.";
					if (dict > 0 && mem >= 0 && dict + (64 << 20) > mem && !ignoreMemory)
					{
						message = "Unpacking it takes " + fetch::bytes(dict) + " of memory, and " + fetch::bytes(mem) +
							" is free: it may fail. Press A again to try anyway.";
						armed = T_GO;
						return;
					}
					extractJob();
				}
				return;
			case PUT:
				if (!ok) { message = text; putPassword.clear(); putTyped = false; return; }
				if (putTyped && last.size() > 1) typed[last[1]] = putPassword;
				putPassword.clear();
				putTyped = false;
				loadServers();
				log("server " + (last.size() > 1 ? last[1] : std::string("?")) + " saved");
				ui->filesRoot(last.size() > 1 ? "server:" + last[1] : "", "Saved.");
				return;
			case FORGET:
				loadServers();
				ui->filesRoot("", "Deleted.");
				return;
			case TRUST:
				connect(srv);
				return;
			case RECONNECT:
				// (ok: the request behind it goes now; else it is dropped, and the login says why)
				if (ok) { connectedId = srv.id; return; }
				queuedWhat = NOTHING;
				queued.clear();
				if (word == "password" || word == "auth") { up = false; typed.erase(srv.id); askLogin(); return; }
				message = text;
				if (up) { up = false; ui->filesRoot("server:" + srv.id, message); }	// (the folder above, for B, can't come)
				return;
			default:
				return;
			}
		}

		// ---- target: where a download (or an unpacking) goes ----------------------------------------------------------
		// what the chosen entries may hold: zips, and other archives (7z, RAR; a folder may hold either)
		void archivesIn(bool& zips, bool& others) const
		{
			zips = others = false;
			if (local)
			{
				for (auto& a : archives) (fetch::kindOf(fetch::baseName(a)) == fetch::ZIP ? zips : others) = true;
				return;
			}
			for (auto& it : chosen)
			{
				bool any = it.dir || it.link;
				fetch::Kind k = any ? fetch::NONE : fetch::kindOf(it.name);
				if (any || k == fetch::ZIP) zips = true;
				if (any || (k != fetch::NONE && k != fetch::ZIP)) others = true;
			}
		}

		// the system whose folder the target is (or is in) plays its games zipped
		bool playsZip() const
		{
			int s = sysfolders::systemOf(folders, relOf(dest));
			return s >= 0 && folders[s].playsZip;
		}

		// the unpacking rows as Options has them. Zips for a system whose emulator plays them zipped are kept as they are
		// unless asked: by Unpack archives itself when the choice is zips alone, else by a row of their own (7z and RAR
		// archives, which it doesn't play, are unpacked all the same)
		void defaults()
		{
			std::string mode = option("files_unpack", "ask"), delMode = option("files_delete_archive", "ask");
			bool zips, others;
			archivesIn(zips, others);
			bool plays = playsZip();
			if (local) { askUnpack = askZips = false; unpack = true; }
			else
			{
				askUnpack = mode == "ask" && (zips || others);
				unpack = mode == "always" ? true : mode == "never" ? false : others || !plays;
				askZips = askUnpack && plays && zips && others;
			}
			unpackZips = false;
			askDelete = delMode == "ask" && (zips || others || local);
			del = delMode == "always";
		}

		// the job's zips left as they are: the folder's system plays them zipped, and nobody asked for them unpacked
		bool keepZips() const
		{
			bool zips, others;
			archivesIn(zips, others);
			return !local && playsZip() && (askZips ? !unpackZips : !(askUnpack && !others));
		}

		// where this server folder's downloads went last time (still a folder on a card, or a system's), else where its
		// first file belongs, else the roms folder
		void guess()
		{
			std::string last = srv.target(chosenDir);
			size_t colon = last.find(':');
			if (colon != std::string::npos)
			{
				std::string id = last.substr(0, colon), rel = last.substr(colon + 1);
				for (auto& c : cards)
				{
					std::string p = rel.empty() ? c.root : c.root + "/" + rel;
					if (c.id == id && (isDir(p) || sysfolders::byRel(folders, rel) >= 0) && !inBanana(p)) { dest = p; how = LAST; return; }
				}
			}
			std::string root = cards.empty() ? ui->romRoot : cards[0].root;
			int g = -1;
			if (!chosen.empty())
			{
				const Item& it = chosen[0];
				g = sysfolders::suggest(folders, it.dir ? "" : it.name, it.dir ? fetch::joinPath(chosenDir, it.name) : chosenDir);
			}
			how = g >= 0 ? BY_TYPE : ROMS;
			dest = root + "/" + (g >= 0 ? folders[g].rel : std::string("roms"));
		}

		void startTarget(const std::vector<Item>& what, const std::string& from, From o, const std::string& here)
		{
			local = false;
			chosen = what;
			chosenDir = from;
			origin = o;
			originDir = here;
			walked = existingAsked = false;
			existing = SKIP;
			files.clear();
			if (o == FROM_FILES) { dest = here; how = HERE; }
			else guess();
			defaults();
			sel[TARGET] = top[TARGET] = 0;
			message.clear();
			go(TARGET);
		}

		// the target changed (chosen in Files, or on the other card): listed again, the unpacking rows as for it
		void retarget(const std::string& d, How h)
		{
			dest = d;
			how = h;
			walked = existingAsked = inspected = false;
			existing = SKIP;
			// (what was chosen on the page stays, where the folder still asks it)
			bool u = unpack, x = del;
			defaults();
			if (askUnpack) unpack = u;
			if (askDelete) del = x;
		}

		// the folder named after the target's system, from the server: its files go into the target itself (the server's
		// GBA into roms/gba); any other folder chosen takes a server's folder as a folder, as a paste does
		std::string mergeNames() const
		{
			const files::Card* c = cardOf(dest);
			int f = c ? sysfolders::byRel(folders, relOf(dest)) : -1;
			std::string merge;
			if (f >= 0) for (auto& n : folders[f].names) merge += (merge.empty() ? "" : ",") + n;
			return merge;
		}

		void walk()
		{
			std::vector<std::string> req = { "walk", chosenDir, dest, option("files_hidden", "off") == "on" ? "1" : "0", mergeNames() };
			for (auto& it : chosen)
				req.push_back(std::string(it.dir ? "d" : it.link ? "l" : "f") + ":" + (it.dir || it.link ? "" : std::to_string(it.size)) + ":" + it.name);
			if (ask(WALK, req)) message = "Listing the files...";
		}

		// where a job opened from here leaves to: the server's folder, or Files'
		void startedFrom()
		{
			statusFrom = origin;
			statusDir = originDir;
			sel[STATUS] = top[STATUS] = 0;
			pollStatus(true);
			go(STATUS);
		}

		void download()
		{
			std::string why = destProblem();
			if (!why.empty()) { message = why; return; }
			const files::Card& card = *cardOf(dest);
			std::vector<File> todo;
			std::set<std::string> taken;
			int64_t need = 1 << 20, largest = 0;
			for (auto f : files)
			{
				if (isFile(f.local))
				{
					if (existing == SKIP) continue;
					if (existing == KEEP_BOTH)
					{
						// (another name beside it: name (2).ext, as Files' Keep both)
						std::string d = fetch::parentPath(f.local), n = files::freeName(d, fetch::baseName(f.local));
						if (!taken.insert(d + "/" + n).second) continue;
						f.local = d + "/" + n;
					}
				}
				todo.push_back(f);
				if (f.size > 0) { need += f.size - std::min(f.size, sizeOf(f.local + ".part")); largest = std::max(largest, f.size); }
			}
			if (todo.empty()) { message = "Everything chosen is there already."; return; }
			fetch::Space sp;
			if (fetch::spaceAt(dest, sp))
			{
				if ((int64_t)sp.free < need) { message = "Not enough room on " + card.label + ": " + fetch::bytes(need) + " needed, " + fetch::bytes((long long)sp.free) + " free."; return; }
				if (sp.fat32 && (uint64_t)largest > fetch::FAT32_MAX) { message = "A file is " + fetch::bytes(largest) + ": this card (FAT32) takes 4 GB a file at most."; return; }
			}
			fetch::Job j;
			j.title = chosen.size() == 1 ? chosen[0].name : plural((long long)todo.size(), "file", "files") + " from " + srv.title();
			j.server = srv.id;
			j.unpack = unpack;
			j.keepZips = unpack && keepZips();
			j.deleteArchive = unpack && del;
			j.unwrap = option("files_unwrap", "on") == "on";
			j.tempRoot = card.root;
			j.cardDev = (uint64_t)card.dev;
			for (auto& f : todo)
			{
				fetch::JobItem it;
				it.size = f.size;
				it.remote = f.remote;
				it.local = f.local;
				j.items.push_back(it);
			}
			std::string path = nas::newJobPath(paths);
			if (!fetch::writeJob(path, j)) { message = "Couldn't write the job in " + paths.jobs() + "."; return; }
			log(path + ": " + std::to_string(todo.size()) + " files from " + srv.title() + " into " + dest);
			// a password typed for this session goes to the runner, in a file only it reads (once)
			auto t = typed.find(srv.id);
			if (!srv.guest && t != typed.end()) nas::writeSecret(paths.password(srv.id), t->second);
			// where this folder's downloads went: the same next time
			std::string rel = relOf(dest), there = card.id + ":" + rel;
			if (srv.target(chosenDir) != there)
			{
				// (the list's copy too: the server opened again this time goes to the same place)
				for (fetch::ServerRec* s : { &srv, serverById(srv.id) })
				{
					if (!s) continue;
					bool had = false;
					for (auto& x : s->targets) if (x.first == chosenDir) { x.second = there; had = true; }
					if (!had) s->targets.push_back({ chosenDir, there });
				}
				ask(REMEMBER, { "target", srv.id, chosenDir, there });
			}
			if (!running()) nas::startRunner(prog, paths.data, sync);
			marked.clear();
			message.clear();
			startedFrom();
		}

		// ---- unpacking archives on a card --------------------------------------------------------------------------------
		void startUnpack(const std::vector<std::string>& which, const std::string& here)
		{
			local = true;
			archives = which;
			chosen.clear();
			origin = FROM_FILES;
			originDir = here;
			archivePassword.clear();
			ignoreMemory = inspected = false;
			existingAsked = false;
			// into the folder they are in; one of banana/'s (Bshare's received files) goes where its name says
			dest = here;
			how = HERE;
			if (inBanana(dest))
			{
				int g = sysfolders::suggest(folders, fetch::baseName(which[0]), "");
				const files::Card* c = cardOf(here);
				dest = (c ? c->root : ui->romRoot) + "/" + (g >= 0 ? folders[g].rel : std::string("roms"));
				how = g >= 0 ? BY_TYPE : ROMS;
			}
			defaults();
			sel[TARGET] = top[TARGET] = 0;
			message.clear();
			go(TARGET);
		}

		// one archive looked at first (a password asked when it has one, its memory), then unpacked; several go as they are
		// (one with a password is left packed: its row on the job's page unpacks it from the card)
		void unpackGo()
		{
			std::string why = destProblem();
			if (!why.empty()) { message = why; return; }
			if (archives.size() == 1 && !inspected)
			{
				std::vector<std::string> req = { "inspect", archives[0] };
				if (!archivePassword.empty()) req.push_back(archivePassword);
				if (ask(INSPECT, req)) message = "Looking into " + fetch::baseName(archives[0]) + "...";
				return;
			}
			extractJob();
		}

		void extractJob()
		{
			std::string why = destProblem();
			if (!why.empty()) { message = why; return; }
			const files::Card& card = *cardOf(dest);
			fetch::Job j;
			j.title = archives.size() == 1 ? "Unpack " + fetch::baseName(archives[0]) : "Unpack " + plural((long long)archives.size(), "archive", "archives");
			j.unpack = true;
			j.deleteArchive = del;
			j.unwrap = option("files_unwrap", "on") == "on";
			j.ignoreMemory = ignoreMemory;
			j.tempRoot = card.root;
			j.cardDev = (uint64_t)card.dev;
			for (auto& a : archives)
			{
				fetch::JobItem it;
				it.extract = true;
				it.remote = a;
				it.local = dest;
				j.items.push_back(it);
			}
			std::string path = nas::newJobPath(paths);
			if (!archivePassword.empty())
			{
				j.archivePasswordFile = paths.run + "/fetch-" + path.substr(path.rfind('/') + 1) + ".apw";
				nas::writeSecret(j.archivePasswordFile, archivePassword);
			}
			if (!fetch::writeJob(path, j)) { message = "Couldn't write the job in " + paths.jobs() + "."; return; }
			log(path + ": " + plural((long long)archives.size(), "archive", "archives") + " unpacked into " + dest);
			archivePassword.clear();
			ignoreMemory = false;
			if (!running()) nas::startRunner(prog, paths.data, sync);
			message.clear();
			startedFrom();
		}

		// ---- the server editor ---------------------------------------------------------------------------------------
		// what a text field is (the editor's top panel and the keyboard say it)
		static const char* fieldHelp(int kind)
		{
			switch (kind)
			{
			case E_NAME: return "What the list calls it (empty: its address).";
			case E_HOST: return "The NAS's or the computer's IP address on your network, like 192.168.1.10, or its name.";
			case E_PORT: return "SMB 445, SFTP 22, FTP 21: change it only when the server uses another.";
			case E_USER: return "The account's name on the server.";
			case E_PASSWORD: return "The account's password. Kept sealed on the card when Remember password is on.";
			case E_DOMAIN: return "Only for an account in a Windows domain: usually empty.";
			case E_START: return "Where its folders open, like /Games (on SMB the share first: /Games is the share Games). Empty: where the login lands.";
			default: return "";
			}
		}

		void edit(const fetch::ServerRec* s)
		{
			existingServer = s != nullptr;
			draft = s ? *s : fetch::ServerRec();
			if (!s) { draft.protocol = "smb"; draft.remember = true; draft.encoding = "utf-8"; }
			draftPassword.clear();
			passwordTyped = false;
			portTouched = s && s->port > 0 && s->port != s->defaultPort();
			sel[EDIT] = top[EDIT] = 0;
			message.clear();
			go(EDIT);
		}

		void save()
		{
			if (draft.host.empty()) { message = "The server's address is needed: an IP address like 192.168.1.10, or its name."; return; }
			bool plainFtp = draft.protocol == "ftp" && !draft.tls && !draft.guest;
			if (plainFtp && armed != E_SAVE)
			{
				message = "Plain FTP sends the password unencrypted: prefer FTPS or SFTP when the server has them. Press A again to save anyway.";
				armed = E_SAVE;
				return;
			}
			armed = -1;
			std::vector<std::string> req = { "put" };
			if (existingServer) req.push_back("id=" + draft.id);
			req.push_back("name=" + draft.name);
			req.push_back("protocol=" + draft.protocol);
			req.push_back("host=" + draft.host);
			req.push_back("port=" + std::to_string(draft.port > 0 ? draft.port : draft.defaultPort()));
			req.push_back(std::string("guest=") + (draft.guest ? "yes" : "no"));
			req.push_back("user=" + draft.user);
			req.push_back(std::string("remember=") + (draft.remember ? "yes" : "no"));
			req.push_back("domain=" + draft.domain);
			req.push_back("start=" + draft.start);
			req.push_back(std::string("tls=") + (draft.tls ? "yes" : "no"));
			req.push_back("encoding=" + (draft.encoding.empty() ? std::string("utf-8") : draft.encoding));
			if (passwordTyped) req.push_back("password=" + draftPassword);
			// a password not kept serves this time still (the server's id is in the answer, for a new one)
			putTyped = passwordTyped && !draft.remember && !draft.guest;
			putPassword = putTyped ? draftPassword : "";
			if (ask(PUT, req)) message = "Saving...";
		}

		void change(int kind, int d)
		{
			fetch::ServerRec& s = draft;
			switch (kind)
			{
			case E_PROTOCOL:
			{
				static const char* const P[] = { "smb", "sftp", "ftp" };
				int at = s.protocol == "sftp" ? 1 : s.protocol == "ftp" ? 2 : 0;
				int was = s.defaultPort();
				s.protocol = P[(at + d + 3) % 3];
				if (!portTouched || s.port == was) s.port = 0;	// (the port follows the protocol unless it was set)
				if (s.protocol == "sftp") s.guest = false;
				break;
			}
			case E_GUEST: s.guest = !s.guest; break;
			case E_REMEMBER: s.remember = !s.remember; break;
			case E_TLS: s.tls = !s.tls; break;
			case E_ENCODING:
			{
				static const char* const C[] = { "utf-8", "cp1251", "cp866" };
				int at = s.encoding == "cp1251" ? 1 : s.encoding == "cp866" ? 2 : 0;
				s.encoding = C[(at + d + 3) % 3];
				break;
			}
			case O_UNPACK: case O_DELETE:
			{
				static const char* const V[] = { "ask", "always", "never" };
				const char* key = kind == O_UNPACK ? "files_unpack" : "files_delete_archive";
				std::string v = option(key, "ask");
				int at = v == "always" ? 1 : v == "never" ? 2 : 0;
				setConfValue(paths.data + "/settings.conf", key, V[(at + d + 3) % 3]);
				break;
			}
			case O_UNWRAP: setConfValue(paths.data + "/settings.conf", "files_unwrap", option("files_unwrap", "on") == "on" ? "off" : "on"); break;
			case O_HIDDEN: setConfValue(paths.data + "/settings.conf", "files_hidden", option("files_hidden", "off") == "on" ? "off" : "on"); break;
			case T_CARD:
			{
				// the same folder on the other card (made when the download starts, if it isn't there)
				const files::Card* c = cardOf(dest);
				if (!c || cards.size() < 2) break;
				int at = 0;
				for (size_t i = 0; i < cards.size(); i++) if (&cards[i] == c) at = (int)i;
				const files::Card& to = cards[(at + d + (int)cards.size()) % (int)cards.size()];
				std::string rel = relOf(dest);
				retarget(rel.empty() ? to.root : to.root + "/" + rel, how);
				break;
			}
			case T_UNPACK: unpack = !unpack; break;
			case T_ZIPS: unpackZips = !unpackZips; break;
			case T_DELETE: del = !del; break;
			case T_EXISTING: existing = (Existing)((existing + d + 3) % 3); break;
			default: break;
			}
		}

		// ---- keys -----------------------------------------------------------------------------------------------------
		Result press(Key k)
		{
			if (waiting() && k != Key::B) return NONE;	// (an answer is coming)
			std::vector<Row> r = rows();
			int n = (int)r.size();
			int& s = sel[sub];
			int& t = top[sub];
			if (s >= n) s = std::max(0, n - 1);
			const Row* cur = n ? &r[s] : nullptr;
			auto moveTo = [&](int to)
			{
				if (!n) return NONE;
				s = (to % n + n) % n;
				if (s < t) t = s;
				if (s >= t + ROWS) t = s - ROWS + 1;
				if (armed >= 0 && armed != K_TRUST) armed = -1;
				return MOVED;
			};
			// a page up or down (the cursor where it was on the screen, the window at the list's ends), or to the first or
			// last row (end): no wrap, as they repeat while held (Files' own lists turn their pages the same way)
			auto page = [&](int d, bool end)
			{
				if (!n) return NONE;
				int last = std::max(0, n - ROWS);
				if (end) { s = d < 0 ? 0 : n - 1; t = d < 0 ? 0 : last; }
				else { s = std::max(0, std::min(n - 1, s + d * ROWS)); t = std::max(0, std::min(last, t + d * ROWS)); }
				if (s < t) t = s;
				if (s >= t + ROWS) t = s - ROWS + 1;
				if (armed >= 0 && armed != K_TRUST) armed = -1;
				return MOVED;
			};
			switch (k)
			{
			case Key::UP: return moveTo(s - 1);
			case Key::DOWN: return moveTo(s + 1);
			case Key::L: return page(-1, true);
			case Key::R: return page(1, true);
			case Key::START:
				if (sub == EDIT) { save(); return MOVED; }
				if (sub == TARGET) return activate(T_GO, nullptr);
				ui->close();
				return CLOSED;
			case Key::B: return back();
			// LEFT and RIGHT: a choice's other value, else a page of the list (a server's folder of thousands of games).
			// Neither goes up or in, nor chooses an action: held, they repeat
			case Key::LEFT:
				if (cur && cur->choice) { change(cur->kind, -1); armed = -1; return ACTED; }
				return page(-1, false);
			case Key::RIGHT:
				if (cur && cur->choice) { change(cur->kind, 1); armed = -1; return ACTED; }
				return page(1, false);
			case Key::A:
				if (cur) return activate(cur->kind, cur);
				if (sub == STATUS) return activate(S_ITEM, nullptr);
				if (sub == BROWSE && armed == K_TRUST) return activate(K_TRUST, nullptr);
				if (sub == BROWSE && !listed) { connect(srv); return MOVED; }	// (not in: the login again)
				return NONE;
			case Key::X:
				if (sub == BROWSE) return openActions(cur);
				if (sub == STATUS && running() && !finished && cancelling.empty())
				{
					if (armed != S_ITEM) { armed = S_ITEM; message = "Press X again to stop. The files downloaded so far are kept, and Resume goes on."; return MOVED; }
					armed = -1;
					stop();
					return ACTED;
				}
				return NONE;
			case Key::Y:
				if (sub == STATUS && canCancel())
				{
					if (armed != S_CANCEL)
					{
						armed = S_CANCEL;
						message = "Press Y again to cancel this job: what it hasn't finished is deleted (the files it finished stay on the card).";
						return MOVED;
					}
					armed = -1;
					cancel();
					return ACTED;
				}
				if (sub == BROWSE && cur)
				{
					const Item& it = items[cur->index];
					if (marked.count(it.name)) marked.erase(it.name); else marked.insert(it.name);
					message.clear();
					return MOVED;
				}
				return NONE;
			case Key::SELECT:
				if (sub == BROWSE && listed) { list(dir); return MOVED; }
				return NONE;
			default:
				return NONE;
			}
		}

		// what is marked (or the row under the cursor): Download, Copy, Mark all
		Result openActions(const Row* cur)
		{
			targets.clear();
			for (auto& it : items) if (marked.count(it.name)) targets.push_back(it);
			if (targets.empty() && cur) targets.push_back(items[cur->index]);
			if (targets.empty()) return NONE;
			sel[ACTIONS] = top[ACTIONS] = 0;
			message.clear();
			go(ACTIONS);
			return MOVED;
		}

		Result activate(int kind, const Row* cur)
		{
			switch (kind)
			{
			case K_TRUST:
				armed = -1;
				ask(TRUST, { "trust", srv.id, keySeen });
				message = "Trusting the new key...";
				return MOVED;
			case B_ITEM:
			{
				const Item& it = items[cur->index];
				if (it.dir) { list(fetch::joinPath(dir, it.name)); return MOVED; }
				if (it.link) { linkTried = it.name; list(fetch::joinPath(dir, it.name)); return MOVED; }
				return openActions(cur);	// (a file: what can be done with it)
			}
			case A_DOWNLOAD: startTarget(targets, dir, FROM_SERVER, ""); return MOVED;
			case A_COPY:
			{
				RemoteClip c;
				c.server = srv.id;
				c.title = srv.title();
				c.dir = dir;
				for (auto& it : targets) c.items.push_back({ it.name, it.dir, it.link, it.dir || it.link ? -1 : it.size });
				ui->setRemoteClip(c);
				marked.clear();
				go(BROWSE);
				message = what(targets) + " to download: open the folder they go to on a card, then X, Paste here.";
				log("copy " + std::to_string(targets.size()) + " from " + srv.title() + " " + dir);
				return ACTED;
			}
			case A_MARKALL: for (auto& it : items) marked.insert(it.name); go(BROWSE); return MOVED;
			case A_UNMARKALL: marked.clear(); go(BROWSE); return MOVED;
			case E_NAME: keyboard(F_NAME, "Server", "Its name", draft.name, fieldHelp(E_NAME)); return MOVED;
			case E_HOST: keyboard(F_HOST, "Server", "Its address", draft.host, fieldHelp(E_HOST)); return MOVED;
			case E_PORT: keyboard(F_PORT, "Server", "Its port", std::to_string(draft.port > 0 ? draft.port : draft.defaultPort()), fieldHelp(E_PORT)); return MOVED;
			case E_USER: keyboard(F_USER, "Server", "The user", draft.user, fieldHelp(E_USER)); return MOVED;
			case E_PASSWORD: keyboard(F_PASSWORD, "Server", "The password", "", fieldHelp(E_PASSWORD)); return MOVED;
			case E_DOMAIN: keyboard(F_DOMAIN, "Server", "Its domain", draft.domain, fieldHelp(E_DOMAIN)); return MOVED;
			case E_START: keyboard(F_START, "Server", "The start folder", draft.start, fieldHelp(E_START)); return MOVED;
			case E_PROTOCOL: case E_GUEST: case E_REMEMBER: case E_TLS: case E_ENCODING:
			case O_UNPACK: case O_DELETE: case O_UNWRAP: case O_HIDDEN:
			case T_CARD: case T_UNPACK: case T_ZIPS: case T_DELETE: case T_EXISTING:
				change(kind, 1);
				return ACTED;
			case T_FOLDER:
			{
				// Files' navigation, from the folder as it is (or the nearest one there): Y chooses, X makes one
				std::string start = dest;
				const files::Card* c = cardOf(dest);
				while (c && !isDir(start) && start.size() > c->root.size()) start = files::parent(start);
				if (!c || inBanana(start)) start = "";
				std::string w = local ? (archives.size() == 1 ? fetch::baseName(archives[0]) : plural((long long)archives.size(), "archive", "archives"))
					: chosen.size() == 1 ? chosen[0].name : plural((long long)chosen.size(), "item", "items") + " from " + srv.title();
				message.clear();
				ui->pickFolder(local ? 2 : 1, start, w);
				return MOVED;
			}
			case E_SAVE: save(); return MOVED;
			case E_DELETE:
				if (armed != E_DELETE) { armed = E_DELETE; message = draft.title() + " goes from the list (its password too). Press A again."; return MOVED; }
				armed = -1;
				ask(FORGET, { "delete", draft.id });
				return MOVED;
			case T_GO:
				if (local)
				{
					if (armed == T_GO) { armed = -1; ignoreMemory = true; extractJob(); return MOVED; }
					unpackGo();
					return MOVED;
				}
				{
					std::string why = destProblem();
					if (!why.empty()) { message = why; return MOVED; }
				}
				if (!walked) { walk(); return MOVED; }
				download();
				return MOVED;
			case S_ITEM:
			{
				// a finished job: A clears it; one stopped or waiting: A goes on; an archive left packed: Files, on it
				if (!cancelling.empty()) return NONE;	// (a job being cancelled isn't resumed)
				const fetch::Outcome* o = cur ? outcome(cur->index) : nullptr;
				if (o && o->what == "packed" && cur->index < (int)job.items.size())
				{
					const fetch::JobItem& it = job.items[cur->index];
					std::string first, key;
					std::string archive = it.extract ? it.remote : fetch::volumeOf(it.local, first, key) ? first : it.local;
					if (isFile(archive)) { ui->filesShow(fetch::parentPath(archive), fetch::baseName(archive), true); return MOVED; }
				}
				if (finished)
				{
					unlink(jobPath.c_str());
					unlink((jobPath + ".done").c_str());
					pollStatus(true);
					if (jobPath.empty() || finished) leaveStatus();
					return MOVED;
				}
				if (!running()) { resume(); return MOVED; }
				return NONE;
			}
			default:
				return NONE;
			}
		}

		Result back()
		{
			armed = -1;
			if (waiting())
			{
				// (a slow server: B gives up on it, and on the way up out of a folder, or before one came, to Files' list)
				bool out = up || (sub == BROWSE && !listed);
				giveUp();
				if (out) ui->filesRoot("server:" + srv.id, message);
				return MOVED;
			}
			switch (sub)
			{
			case BROWSE:
				if (!listed || dir == "/" || dir.empty()) { message.clear(); ui->filesRoot("server:" + srv.id, ""); return MOVED; }
				cursor[dir] = { sel[BROWSE], top[BROWSE] };
				// the folder above; if it can't come (the server unreachable, Wi-Fi off), Files' list: B always gets out
				if (list(fetch::parentPath(dir))) up = true;
				else ui->filesRoot("server:" + srv.id, message);
				return MOVED;
			case ACTIONS: message.clear(); go(BROWSE); return MOVED;
			case TARGET:
				message.clear();
				if (origin == FROM_FILES) ui->filesShow(originDir, "", false);
				else go(BROWSE);
				return MOVED;
			case STATUS: leaveStatus(); return MOVED;
			case EDIT: message.clear(); ui->filesRoot(existingServer ? "server:" + draft.id : "add", ""); return MOVED;
			case OPTIONS: message.clear(); ui->filesRoot("options", ""); return MOVED;
			default: return MOVED;
			}
		}

		Result touch(float cx, float cy)
		{
			float dx = ui->downX, dy = ui->downY;
			std::vector<Row> r = rows();
			// the buttons: the one tapped (or the nearest), where render drew it
			if (dy >= 418 && dy <= 466)
			{
				Key k = ui->chipAt(chips(r), dx);
				return k == Key::NONE ? NONE : press(k);
			}
			if (waiting()) return NONE;
			int& s = sel[sub];
			int& t = top[sub];
			// a swipe up or down scrolls a page, wherever it started (first: one that starts on a row isn't a tap on it)
			if (std::fabs(cy - dy) > 40)
			{
				if (r.empty()) return NONE;
				int n = (int)r.size();
				t = std::max(0, std::min(std::max(0, n - ROWS), t + (cy < dy ? ROWS : -ROWS)));
				s = std::max(t, std::min(s, t + ROWS - 1));
				if (armed != K_TRUST) armed = -1;
				return MOVED;
			}
			for (int i = 0; i < ROWS && t + i < (int)r.size(); i++)
			{
				float ry = 82 + i * 64;
				if (dy < ry || dy > ry + 56) continue;
				int at = t + i;
				// browse: the tick's place marks and unmarks
				if (sub == BROWSE && dx < 70) { s = at; return press(Key::Y); }
				if (at != s) { s = at; if (armed >= 0 && armed != K_TRUST) armed = -1; return MOVED; }
				if (r[at].choice) return press(dx < 320 ? Key::LEFT : Key::RIGHT);
				return press(Key::A);
			}
			return NONE;
		}

		// ---- the keyboard's text --------------------------------------------------------------------------------------
		Result entered(const std::string& text)
		{
			Field f = field;
			field = F_NONE;
			ui->page = SERVERS;
			switch (f)
			{
			case F_NAME: draft.name = text; break;
			case F_HOST:
			{
				std::string h = text;
				h.erase(0, h.find_first_not_of(' '));
				while (!h.empty() && h.back() == ' ') h.pop_back();
				draft.host = h;
				break;
			}
			case F_PORT:
			{
				int p = atoi(text.c_str());
				if (p > 0 && p < 65536) { draft.port = p == draft.defaultPort() ? 0 : p; portTouched = p != draft.defaultPort(); }
				break;
			}
			case F_USER: draft.user = text; break;
			case F_PASSWORD: draftPassword = text; passwordTyped = true; break;
			case F_DOMAIN: draft.domain = text; break;
			case F_START: draft.start = text.empty() ? "" : fetch::cleanPath(text); break;
			case F_LOGIN:
				// (nothing typed: the account's password is empty, which the server takes or refuses)
				typed[srv.id] = text;
				connect(srv);
				return MOVED;
			case F_STATUS:
				if (!job.server.empty())
				{
					typed[job.server] = text;
					nas::writeSecret(paths.password(job.server), text);
					nas::startRunner(prog, paths.data, sync);
					pollStatus(true);
				}
				return MOVED;
			case F_ARCHIVE:
				archivePassword = text;
				inspected = false;
				unpackGo();
				return MOVED;
			default: break;
			}
			return ACTED;
		}

		// ---- what the top panel says ------------------------------------------------------------------------------
		struct Top { std::string label = "Files", title, accent, help; bool meters = false; double file = 0, all = 0; std::string fileLabel; };

		Top topPanel(const std::vector<Row>& r)
		{
			Top t;
			const Row* cur = sel[sub] < (int)r.size() ? &r[sel[sub]] : nullptr;
			switch (sub)
			{
			case EDIT:
				t.label = existingServer ? "Change a server" : "Add a server";
				t.title = draft.title().empty() ? "A new server" : draft.title();
				t.accent = draft.host.empty() ? "" : address(draft);
				if (cur)
					switch (cur->kind)
					{
					case E_PROTOCOL: t.help = "SMB: Windows' and macOS's file sharing, and most NAS (SMB 2 and 3). SFTP: SSH's file transfer, with a password. FTP: FTP or FTPS, passive."; break;
					case E_GUEST: t.help = draft.protocol == "ftp" ? "Anonymous: the server's public files, no account." : "Guest: the share without an account (a NAS's public folder)."; break;
					case E_REMEMBER: t.help = "Yes: kept on the card, sealed for this handheld (another one, or a computer, can't read it). No: asked each time."; break;
					case E_TLS: t.help = "FTPS: the login and the files encrypted (explicit TLS). Its certificate is trusted the first time and checked after."; break;
					case E_ENCODING: t.help = "How an old server writes names in Cyrillic: UTF-8 for any recent one, CP1251 or CP866 for an old Windows one."; break;
					case E_SAVE: t.help = "START saves too."; break;
					case E_DELETE: t.help = "The server goes from the list, its password with it."; break;
					default: t.help = std::string(fieldHelp(cur->kind)) + " A types it."; break;
					}
				break;
			case BROWSE:
			{
				t.label = srv.title();
				t.title = dir.empty() ? srv.title() : dir;
				if (!listed && !waiting()) { t.help = "Not connected: A tries again. B goes back."; break; }
				if (cur)
				{
					const Item& it = items[cur->index];
					t.title = it.name;
					t.accent = it.dir ? "Folder" : it.link ? "Link" : fetch::bytes(it.size) + (it.mtime ? " \xc2\xb7 " + date(it.mtime) : "");
				}
				int64_t bytes = 0;
				for (auto& it : items) if (marked.count(it.name) && !it.dir && it.size > 0) bytes += it.size;
				if (!marked.empty()) t.help += plural((long long)marked.size(), "item", "items") + " marked" + (bytes ? ", " + fetch::bytes(bytes) : "") + ". ";
				t.help += "A opens a folder. Y marks, X: download what is marked (a folder with all in it), or copy it to paste in a card's folder. SELECT reads the folder again, B goes up.";
				break;
			}
			case ACTIONS:
				t.label = srv.title() + " " + dir;
				t.title = cur ? cur->label : "";
				t.accent = what(targets);
				if (cur && cur->kind == A_DOWNLOAD)
				{
					// where it goes, as the Download page will say it first
					std::vector<Item> c = chosen;
					std::string cd = chosenDir, d = dest;
					How h = how;
					chosen = targets;
					chosenDir = dir;
					guess();
					t.help = "Into " + where(dest) + (how == BY_TYPE ? ", by the files' type" : how == LAST ? ", as last time" : "") +
						": the next page says where, and any folder on a card can be chosen there.";
					chosen = c; chosenDir = cd; dest = d; how = h;
				}
				else if (cur && cur->kind == A_COPY) t.help = "Into Files' clipboard: then open a folder on a card, and X, Paste here downloads them there.";
				else t.help = "Every entry of this folder, for Download or Copy.";
				break;
			case TARGET:
				t.label = local ? "Unpack" : "Download";
				t.title = local ? (archives.size() == 1 ? fetch::baseName(archives[0]) : plural((long long)archives.size(), "archive", "archives"))
					: chosen.size() == 1 ? chosen[0].name : plural((long long)chosen.size(), "item", "items");
				t.accent = where(dest);
				if (walked)
				{
					int64_t b = 0;
					for (auto& f : files) if (f.size > 0) b += f.size;
					t.accent = plural((long long)files.size(), "file", "files") + ", " + fetch::bytes(b);
				}
				t.help = local ? "Into the folder chosen. Archives of one folder are unwrapped (Options)."
					: "Into the folder chosen. Archives are unpacked after their last part comes in.";
				if (cur && cur->kind == T_FOLDER)
					t.help = (how == BY_TYPE ? "The files' type says it. " : how == LAST ? "Where this server folder's downloads went last time. " : "") +
						std::string("A chooses another folder on the cards, or makes one: Files opens on it, Y takes the folder shown, X makes a new one.") +
						(!local && mergeNames().empty() ? " A server's folder goes in as a folder." : !local ? " The server's folder of this system goes into it itself." : "");
				if (cur && cur->kind == T_CARD) t.help = "The same folder on the other card (made when it starts, if it isn't there).";
				if (cur && cur->kind == T_UNPACK)
					t.help = askZips ? "Yes: 7z and RAR archives unpacked into the folder, then (Delete archives after) deleted. This system plays its games zipped: its zips are kept as they are unless the next row says otherwise."
						: "Yes: archives unpacked into the folder, then (Delete archives after) deleted. Zips for a system that plays them zipped are kept as they are by default.";
				if (cur && cur->kind == T_ZIPS) t.help = "This system plays its games zipped, so its zips are kept as they are, the 7z and RAR archives unpacked all the same. Unpack them too: the zips as well.";
				if (cur && cur->kind == T_EXISTING) t.help = "Skip them: those stay as they are. Replace them: the new ones over the old. Keep both: the new ones get another name (2).";
				if (local && archives.size() > 1 && cur && cur->kind == T_GO) t.help = "Each archive into the folder chosen. One with a password stays packed: its row on the next page unpacks it, the password typed then.";
				break;
			case STATUS:
			{
				t.label = isUnpack() ? "Unpacking" : "Downloading";
				t.title = job.title.empty() ? "Nothing to do" : job.title;
				t.accent = jobState(false);
				t.meters = !finished && running() && st.n > 0;
				t.all = st.n > 0 ? std::min(1.0, (double)outcomes.size() / st.n) : 0;
				t.file = st.state == "unpack" ? st.percent / 100.0 : st.total > 0 ? (double)st.done / st.total : 0;
				t.fileLabel = st.name;
				if (cur && cur->index >= 0)
				{
					const fetch::Outcome* o = outcome(cur->index);
					if (o && (o->what == "failed" || o->what == "packed"))
					{
						// ("<word>: <what happened>": the words)
						size_t at = o->detail.find(": ");
						std::string d = at == std::string::npos ? o->detail : o->detail.substr(at + 2);
						t.help = cur->label + ": " + d + (o->what == "packed" ? " A shows it in Files, to unpack it from there." : "");
						break;
					}
				}
				t.help = finished ? "A clears it (the files stay on the card). Your games show in the menu when you go back to it."
					: !cancelling.empty() ? "The job stops, then what it hasn't finished is deleted."
					: st.state == "need" ? "A types the password. Y cancels the download."
					: !running() ? "A goes on from where it stopped. Y cancels it: what it hasn't finished is deleted."
					: "B goes back: it carries on meanwhile, also while you play (paused) or the menu shows anything else. X stops it, Y cancels it.";
				if (jobsBehind > 0) t.help += " " + plural(jobsBehind, "more job waits", "more jobs wait") + " after this one.";
				break;
			}
			case OPTIONS:
				t.label = "Download options";
				t.title = cur ? cur->label : "";
				t.accent = cur ? cur->value : "";
				if (cur)
					switch (cur->kind)
					{
					case O_UNPACK: t.help = "Ask: the Download page asks each time. Always or Never: it doesn't ask."; break;
					case O_DELETE: t.help = "An archive unpacked is deleted after (all its parts): ask each time, always or never."; break;
					case O_UNWRAP: t.help = "An archive holding a single folder: its contents go into the folder chosen, not the folder itself."; break;
					default: t.help = "Files and folders whose names start with a dot, on the servers (the cards' stay hidden in Files)."; break;
					}
				break;
			default:
				break;
			}
			if (!message.empty()) t.help = message;
			return t;
		}

		std::string header() const
		{
			switch (sub)
			{
			case EDIT: return existingServer ? "Change the server" : "A new server";
			case BROWSE: return srv.title() + ": " + (dir.empty() ? "..." : dir);
			case ACTIONS: return "What to do";
			case TARGET: return local ? "Unpack where" : "Download where";
			case STATUS: return finished ? (isUnpack() ? "Last unpacking" : "Last download") : isUnpack() ? "Unpacking" : "Downloading";
			case OPTIONS: return "Download options";
			default: return "";
			}
		}

		std::string emptyText()
		{
			if (waiting()) return "...";
			if (sub == BROWSE) return listed ? "An empty folder." : "Not connected.";
			if (sub == STATUS) return "Nothing to download.";
			return "";
		}

		// ---- drawing ------------------------------------------------------------------------------------------------
		void render()
		{
			SPal p = pal(ui->light);
			// (the cursor kept in its window first: a folder's rows get their words for that window alone)
			int& s = sel[sub];
			int& tp = top[sub];
			if (sub == BROWSE)
			{
				if (s >= (int)items.size()) s = std::max(0, (int)items.size() - 1);
				if (s < tp) tp = s;
				if (s >= tp + ROWS) tp = s - ROWS + 1;
			}
			std::vector<Row> r = rows();
			Top t = topPanel(r);
			rnds::Backend* be = ui->be;
			be->beginScreen(0);
			ui->box("screen", false, 0, 0, 640, 480);
			ui->box("hero", false, 18, 20, 604, 436);
			ui->icon("files", 50, 56, 3);
			ui->text(ui->fit(t.label, true, 17, 420, true, 1.4f), true, 17, 170, 62, p.muted, true, 1.4f);
			ui->text(ui->fit(t.title.empty() ? " " : t.title, true, 30, 420), true, 30, 170, 88, p.ink);
			if (!t.accent.empty()) ui->text(ui->fit(t.accent, true, 22, 420), true, 22, 170, 130, p.accent);
			ui->wrapped(t.help, 20, 50, 196, 540, t.meters ? 4 : 10, message.empty() ? p.muted : p.ink);
			if (t.meters)
			{
				ui->meter(50, 316, 540, t.file);
				ui->text(ui->fit(t.fileLabel, false, 16, 540), false, 16, 50, 338, p.muted);
				ui->meter(50, 376, 540, t.all);
				ui->text(plural((long long)outcomes.size(), "done", "done") + " of " + std::to_string(st.n), false, 16, 50, 398, p.muted);
			}
			be->endScreen();

			be->beginScreen(1);
			ui->box("screen", false, 0, 0, 640, 480);
			ui->box("bubble", false, 18, 14, 604, 54);
			std::string count = r.empty() ? "" : std::to_string(sel[sub] + 1) + " / " + std::to_string(r.size());
			if (sub == BROWSE && !marked.empty()) count = std::to_string(marked.size()) + " marked";
			float cw = ui->text(count, true, 17, 0, 0, p.muted, false, 0, true);
			ui->text(ui->fit(header(), true, 22, 540 - cw), true, 22, 38, 28, p.ink);
			ui->text(count, true, 17, 602 - cw, 31, p.muted);
			if (s >= (int)r.size()) s = std::max(0, (int)r.size() - 1);
			if (s < tp) tp = s;
			if (s >= tp + ROWS) tp = s - ROWS + 1;
			for (int i = 0; i < ROWS && tp + i < (int)r.size(); i++)
			{
				const Row& row = r[tp + i];
				bool on = tp + i == s;
				float y = 82 + i * 64;
				ui->box(on ? "pick" : "slot", on, 18, y, 604, 56);
				Color c = on ? p.onPick : p.muted;
				Color vc = row.warn ? (on ? p.onPickAccent : p.accent) : c;
				float lx = 38;
				if (sub == BROWSE) lx = 62;
				if (row.mark) ui->tick(38, y + 21, row.warn ? vc : (on ? p.onPick : p.accent));
				if (row.mark && sub != BROWSE) lx = 62;
				std::string v = row.value.empty() ? "" : ui->fit(row.value, true, 19, row.choice ? 300 : 330);
				float vw = v.empty() ? 0 : ui->text(v, true, 19, 0, 0, vc, false, 0, true);
				float room = (row.choice ? 556 - vw - 16 : 586 - vw) - lx - 16;
				ui->text(ui->fit(row.label, true, 20, room), true, 20, lx, y + 16, c);
				if (v.empty()) continue;
				if (row.choice)
				{
					ui->arrow(true, 576, y + 21, vc);
					ui->text(v, true, 19, 566 - vw, y + 17, vc);
					ui->arrow(false, 556 - vw - 2 - std::round(13 / 2.0f + 1), y + 21, vc);
				}
				else ui->text(v, true, 19, 586 - vw, y + 17, vc);
			}
			if (r.empty()) ui->text(emptyText(), true, 20, 38, 98, p.muted);
			ui->drawChips(chips(r));
			be->endScreen();
		}

		// the buttons, drawn and tapped from this one list: B on the left (and the page's others after it), A on the right
		std::vector<Settings::Chip> chips(const std::vector<Row>& r)
		{
			const Row* cur = sel[sub] < (int)r.size() ? &r[sel[sub]] : nullptr;
			std::vector<Settings::Chip> cs = { { Key::B, "B", sub == BROWSE && listed && dir != "/" ? "Up" : "Back" } };
			std::string a;
			switch (sub)
			{
			case EDIT:
				a = !cur ? "" : cur->kind == E_SAVE ? "Save" : cur->kind == E_DELETE ? "Delete" : cur->choice ? "Change" : "Type";
				if (cur && cur->kind != E_SAVE) cs.push_back({ Key::START, "START", "Save" });
				break;
			case BROWSE:
				if (armed == K_TRUST) { a = "Trust"; break; }
				if (!listed) { a = waiting() ? "" : "Connect"; break; }
				a = !cur ? "" : items[cur->index].dir || items[cur->index].link ? "Open" : "Actions";
				if (cur) cs.push_back({ Key::Y, "Y", marked.count(items[cur->index].name) ? "Unmark" : "Mark" });
				if (cur) cs.push_back({ Key::X, "X", "Actions" });
				break;
			case ACTIONS: a = cur ? "Choose" : ""; break;
			case TARGET: a = !cur ? "" : cur->kind == T_GO ? (local ? "Unpack" : "Download") : cur->kind == T_FOLDER ? "Choose" : "Change"; break;
			case STATUS:
				a = !cancelling.empty() ? "" : finished ? "Clear" : st.state == "need" ? "Password" : !running() && !jobPath.empty() ? "Resume" : "";
				if (cancelling.empty() && cur && cur->index >= 0 && outcome(cur->index) && outcome(cur->index)->what == "packed") a = "Show";
				if (!finished && running() && cancelling.empty()) cs.push_back({ Key::X, "X", armed == S_ITEM ? "Stop!" : "Stop" });
				if (canCancel()) cs.push_back({ Key::Y, "Y", armed == S_CANCEL ? "Cancel!" : "Cancel" });
				break;
			case OPTIONS: a = cur ? "Change" : ""; break;
			default: break;
			}
			if (!a.empty() && !waiting()) cs.push_back({ Key::A, "A", a, true });
			return cs;
		}

		bool update(double t)
		{
			bool changed = false;
			std::vector<std::vector<std::string>> r;
			std::vector<std::string> last;
			if (session.take(r, last)) { answered(r, last); changed = true; }
			if (queuedWhat != NOTHING && !session.busy())
			{
				Ask w = queuedWhat;
				queuedWhat = NOTHING;
				std::vector<std::string> q;
				q.swap(queued);
				ask(w, q);
			}
			if (cancelled()) changed = true;
			// (the job's page, and Files' row for it, while a job is at work)
			if ((sub == STATUS || (!jobPath.empty() && !finished)) && pollStatus(false)) changed = true;
			(void)t;
			return changed;
		}

		int wake(double t) const
		{
			if (session.answered()) return 0;	// (an answer in: taken at once)
			if (!session.busy() && queuedWhat != NOTHING) return 0;	// (the request behind it goes now)
			int w = -1;
			auto sooner = [&](int ms) { if (ms >= 0 && (w < 0 || ms < w)) w = ms; };
			if (session.busy() && !ui->wakeHost) sooner(50);	// (no one to wake the loop when the answer is in: looked for)
			if (!cancelling.empty()) sooner(100);	// (the runner's end, which the job's deletion waits for)
			if ((sub == STATUS || !finished) && !jobPath.empty()) sooner(std::max(0, (int)(stNext - steady())));
			(void)t;
			return w;
		}
	};

	// ---- Settings' side -------------------------------------------------------------------------------------------
	Settings::Servers& Settings::servers()
	{
		if (!srv)
		{
			srv = std::make_shared<Servers>();
			srv->ui = this;
			const char* prog = getenv("BANANA_FETCH");	// (a stand-in, for the headless test)
			srv->prog = prog && *prog ? prog : "/usr/bin/banana-fetch";
			srv->paths = fetch::Paths::fromEnv(data);
			srv->loadServers();
			srv->pollStatus(true);
		}
		srv->sync = sync;
		return *srv;
	}

	void Settings::serversRefresh()
	{
		Servers& v = servers();
		v.loadServers();
		v.pollStatus(true);
	}

	std::vector<Settings::ServerInfo> Settings::serverList()
	{
		std::vector<ServerInfo> out;
		for (auto& s : servers().servers)
		{
			ServerInfo i;
			i.id = s.id;
			i.title = s.title();
			i.protocol = protoLabel(s);
			i.address = address(s);
			i.plainFtp = s.protocol == "ftp" && !s.tls && !s.guest;
			out.push_back(i);
		}
		return out;
	}

	bool Settings::fetchJobRow(std::string& label, std::string& value, std::string& title, std::string& state, bool& warn)
	{
		Servers& v = servers();
		if (v.jobPath.empty()) return false;
		bool unpacking = v.isUnpack();
		label = v.finished ? (unpacking ? "Last unpacking" : "Last download") : unpacking ? "Unpacking" : "Download";
		value = v.jobState(true);
		title = v.job.title;
		state = v.jobState(false);
		warn = !v.finished;
		return true;
	}

	std::set<std::string> Settings::fetchParts()
	{
		std::set<std::string> out;
		Servers& v = servers();
		if (v.jobPath.empty() || v.finished) return out;
		for (size_t i = 0; i < v.job.items.size(); i++)
			if (!v.job.items[i].extract && !v.outcome((int)i)) out.insert(v.job.items[i].local + ".part");
		return out;
	}

	void Settings::openServer(const std::string& id)
	{
		Servers& v = servers();
		v.prepare();
		fetch::ServerRec* s = v.serverById(id);
		if (!s) return;
		page = SERVERS;
		v.dir.clear();
		v.items.clear();
		v.marked.clear();
		v.listed = false;
		v.message.clear();
		v.sel[Servers::BROWSE] = v.top[Servers::BROWSE] = 0;
		v.go(Servers::BROWSE);
		v.connect(*s);
	}

	void Settings::editServer(const std::string& id)
	{
		Servers& v = servers();
		v.prepare();
		page = SERVERS;
		v.edit(v.serverById(id));
	}

	void Settings::openServerOptions()
	{
		Servers& v = servers();
		v.prepare();
		page = SERVERS;
		v.message.clear();
		v.sel[Servers::OPTIONS] = v.top[Servers::OPTIONS] = 0;
		v.go(Servers::OPTIONS);
	}

	void Settings::openFetchJob()
	{
		Servers& v = servers();
		v.prepare();
		page = SERVERS;
		v.message.clear();
		v.statusFrom = Servers::FROM_ROOT;
		v.sel[Servers::STATUS] = v.top[Servers::STATUS] = 0;
		v.pollStatus(true);
		v.go(Servers::STATUS);
	}

	std::string Settings::pasteDownload(const RemoteClip& c, const std::string& dir)
	{
		Servers& v = servers();
		v.prepare();
		v.loadServers();
		fetch::ServerRec* s = v.serverById(c.server);
		if (!s) return c.title + " isn't in the list of servers any more.";
		// (the server in hand: its session logs in again first when it is another one's, ask())
		if (v.srv.id != s->id) { v.srv = *s; v.keySeen.clear(); }
		std::vector<Servers::Item> what;
		for (auto& i : c.items)
		{
			Servers::Item it;
			it.name = i.name;
			it.dir = i.dir;
			it.link = i.link;
			it.size = i.size;
			what.push_back(it);
		}
		page = SERVERS;
		v.startTarget(what, c.dir, Servers::FROM_FILES, dir);
		return "";
	}

	void Settings::unpackArchives(const std::vector<std::string>& archives, const std::string& dir)
	{
		Servers& v = servers();
		v.prepare();
		page = SERVERS;
		v.startUnpack(archives, dir);
	}

	void Settings::folderPicked(const std::string& dir)
	{
		Servers& v = servers();
		v.prepare();
		page = SERVERS;
		v.retarget(dir, Servers::CHOSEN);
		v.message.clear();
		v.go(Servers::TARGET);
	}

	void Settings::pickCancelled()
	{
		Servers& v = servers();
		page = SERVERS;
		v.go(Servers::TARGET);
	}

	// a card came or went: the target's card, if it went, says so on its page (the download asks for another folder)
	bool Settings::serversCardsChanged()
	{
		if (!srv) return false;
		srv->cards = files::cards(romRoot);
		return page == SERVERS && srv->sub == Servers::TARGET;
	}

	void Settings::closeServers()
	{
		if (srv) srv->close();
	}

	Settings::Result Settings::pressServers(Key k) { return srv ? srv->press(k) : NONE; }
	Settings::Result Settings::touchServers(float cx, float cy) { return srv ? srv->touch(cx, cy) : NONE; }
	void Settings::renderServers() { if (srv) srv->render(); }
	bool Settings::updateServers(double t) { return srv && srv->update(t); }
	int Settings::serversWake(double t) const { return srv ? srv->wake(t) : -1; }

	Settings::Result Settings::finishServersEntry()
	{
		std::string text = pw;
		pw.clear();
		page = SERVERS;
		message.clear();
		return srv ? srv->entered(text) : MOVED;
	}
}
