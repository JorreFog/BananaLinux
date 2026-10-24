// banana-ui: Downloads, the shelf's tile. Games from a NAS or a computer over SMB, SFTP or FTP, and archives unpacked
// (as the settings' pages: a list on the bottom panel, what is chosen explained on the top one).
//   home      the job in hand, the servers, Add a server, Archives on the card, Options
//   edit      a server: its protocol, address, login, start folder...
//   browse    a server's folder: A opens (a file: where it goes), Y marks, X downloads what is marked
//   target    the card and the folder (and unpacking): the files are listed, folders gone through, and the job written
//   status    the job: its files, the one in hand's progress; Stop, Resume, Cancel
//   local     archives on the cards: unpacked here or into a system's folder, or deleted
//   options   unpacking after a download, the archive deleted after, one folder unwrapped, hidden files
// banana-fetch does the network and the unpacking: its session (nas::Session) logs in and lists while the pages are
// open, its runner does the jobs in the background, also while the menu does anything else. This file asks and draws.
#include "settings.h"
#include "settings_defaults.h"
#include "settings_draw.h"
#include "nas.h"
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

	struct Settings::Downloads
	{
		enum Sub { HOME, EDIT, BROWSE, TARGET, STATUS, LOCAL, OPTIONS, SUBS };
		enum Ask { NOTHING, CONNECT, LIST, WALK, INSPECT, PUT, FORGET, TRUST, REMEMBER, RECONNECT };
		enum Field { F_NONE, F_NAME, F_HOST, F_PORT, F_USER, F_PASSWORD, F_DOMAIN, F_START, F_LOGIN, F_STATUS, F_ARCHIVE };
		// what each row does
		enum Kind { K_JOB, K_SERVER, K_ADD, K_LOCAL, K_OPTIONS,
			E_NAME, E_PROTOCOL, E_HOST, E_PORT, E_GUEST, E_USER, E_PASSWORD, E_REMEMBER, E_DOMAIN, E_START, E_TLS, E_ENCODING, E_SAVE, E_DELETE,
			B_ITEM, T_CARD, T_FOLDER, T_UNPACK, T_ZIPS, T_DELETE, T_EXISTING, T_GO, S_ITEM, S_CANCEL, L_CARD, L_ENTRY, L_HERE, L_TO,
			L_AFTER, L_DELETE, O_UNPACK, O_DELETE, O_UNWRAP, O_HIDDEN };

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
		Sub sub = HOME;
		Ask asked = NOTHING;		// what the session is answering
		Ask queuedWhat = NOTHING;	// a request made meanwhile (behind a remembered folder's), sent when that answer is in
		std::vector<std::string> queued;
		int sel[SUBS] = {}, top[SUBS] = {};
		std::string message;		// the top panel's word: what went wrong, what happened
		int armed = -1;				// an action waiting for its second press (its kind)
		Field field = F_NONE;		// what the keyboard is typing

		// home
		std::vector<fetch::ServerRec> servers;
		std::map<std::string, std::string> typed;	// passwords typed this time, by server (the ones not remembered)
		// the server in hand
		fetch::ServerRec srv;
		std::string home, keySeen;	// keySeen: the key it shows now, when that isn't the one pinned (A again trusts it)
		// browse
		std::string dir, listing;	// the folder shown; the one being listed
		std::vector<Item> items;
		std::set<std::string> marked;
		std::map<std::string, std::pair<int, int>> cursor;	// a folder's selection and scroll, to come back to
		std::string linkTried;		// a link opened as a folder: if it doesn't list, it is a file
		// target
		std::vector<nas::Card> cards;
		std::vector<nas::Folder> folders;
		int card = 0, folder = 0;
		bool unpack = true, del = false, replace = false;
		bool askUnpack = false, askDelete = false, existingAsked = false;
		bool askZips = false, unpackZips = false;	// the zips' row (a system that plays them zipped, other archives too)
		bool local = false;			// unpacking an archive on the card (not a download)
		std::string chosenDir;
		std::vector<Item> chosen;
		std::vector<File> files;
		bool walked = false;
		int existing = 0;
		// local
		std::string ldir, archive, archivePassword;	// archive: the one in hand (its actions open)
		std::vector<nas::LocalEntry> lentries;
		bool lmenu = false, ignoreMemory = false;
		bool localDel = false;		// the archive's menu: deleted after it is unpacked (Options' Ask)
		int pendingAction = -1;		// L_HERE or L_TO, waiting for the archive's inspection
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
		// edit
		fetch::ServerRec draft;
		std::string draftPassword, putPassword;
		bool passwordTyped = false, putTyped = false, existingServer = false, portTouched = false;

		// ---- the basics ---------------------------------------------------------------------------------------------
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

		void open()
		{
			sub = HOME;
			message.clear();
			armed = -1;
			loadServers();
			cards = nas::cards(ui->romRoot);
			folders = nas::folders(ui->systemsConf);
			session.onAnswer = ui->wake;	// (the loop woken when an answer is in, rather than looking every 50 ms)
			if (!session.alive() && !session.start(prog, paths.data, sync)) message = "Downloads' helper (" + prog + ") couldn't be started.";
			pollStatus(true);
		}

		void close()
		{
			// (a cancel under way finished first: the runner it stopped ends within a moment, a second at most)
			for (int i = 0; i < 20 && !cancelling.empty() && !cancelled(); i++) usleep(50000);
			session.stop();
			asked = queuedWhat = NOTHING;
			queued.clear();
		}

		bool ask(Ask what, const std::vector<std::string>& req)
		{
			bool fresh = !session.alive();
			if (fresh && !session.start(prog, paths.data, sync)) { message = "Downloads' helper couldn't be started."; return false; }
			if (session.busy()) { queuedWhat = what; queued = req; return true; }
			// a new helper (the last one went after its idle minutes): logged in to the server in hand again first
			if (fresh && (what == LIST || what == WALK) && !srv.id.empty())
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
			asked = queuedWhat = NOTHING;
			queued.clear();
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
			ui->startEntry(ENTRY_DOWNLOADS, title, what, DOWNLOADS, start);
			ui->entryHelp = help + " START when it is typed.";
		}

		// ---- the job and its status ----------------------------------------------------------------------------------
		// the job to show: the one being done (or waiting), else the last finished
		bool pollStatus(bool now)
		{
			double t = steady();
			if (!now && t < stNext) return false;
			stNext = t + 500;
			std::string before = statusLine + "|" + jobPath + "|" + std::to_string(outcomes.size());
			std::vector<std::string> pending = fetch::pendingJobs(paths.jobs()), done = fetch::finishedJobs(paths.jobs());
			jobPath = !pending.empty() ? pending[0] : !done.empty() ? done[0] : "";
			finished = pending.empty() && !jobPath.empty();
			jobsBehind = pending.empty() ? 0 : (int)pending.size() - 1;
			if (jobPath.empty()) job = fetch::Job();
			else if (job.path != jobPath) fetch::readJob(jobPath, job);
			outcomes = jobPath.empty() ? std::vector<fetch::Outcome>() : fetch::readOutcomes(jobPath + ".done");
			statusLine = firstLine(paths.status());
			st = fetch::parseStatus(statusLine);
			return before != statusLine + "|" + jobPath + "|" + std::to_string(outcomes.size());
		}

		bool running() { return nas::runner(paths) != 0; }

		// a job not finished, running or not, can be given up on
		bool canCancel() const { return !finished && !jobPath.empty() && cancelling.empty(); }

		const fetch::Outcome* outcome(int i) const
		{
			for (auto& o : outcomes) if (o.index == i) return &o;
			return nullptr;
		}

		// the job in a few words, for the home row and the status page
		std::string jobState(bool brief)
		{
			if (jobPath.empty()) return "";
			if (finished)
			{
				fetch::Status d = fetch::summary(job, outcomes);
				std::string r = brief ? "Done" : "Finished: " + std::to_string(d.downloaded) + " downloaded";
				if (!brief && d.unpacked) r += ", " + std::to_string(d.unpacked) + " unpacked";
				if (d.failed) r += ", " + std::to_string(d.failed) + " failed";
				if (!brief && d.packed) r += ", " + std::to_string(d.packed) + " left packed";
				return r;
			}
			if (!cancelling.empty()) return brief ? "Cancelling" : "Cancelling: the download stops";
			if (st.state == "need") return brief ? "Needs a password" : "Waiting for the password of " + st.name;
			if (!running()) return brief ? "Stopped" : "Stopped: A goes on from where it was";
			std::string of = std::to_string(st.i) + " of " + std::to_string(st.n);
			if (st.state == "get")
			{
				int pc = st.total > 0 ? (int)(st.done * 100 / st.total) : 0;
				if (brief) return of + " · " + std::to_string(pc) + "%";
				std::string r = "Downloading " + of + ": " + std::to_string(pc) + "%";
				if (st.speed > 0) r += " · " + fetch::bytes(st.speed) + "/s";
				if (st.eta >= 0) r += " · " + duration(st.eta) + " left";
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
			fprintf(stderr, "downloads: %s cancelled\n", cancelling.c_str());
			cancelling.clear();
			message = "Cancelled. The files it had finished stay on the card.";
			bool more = !fetch::pendingJobs(paths.jobs()).empty();
			if (restartAfter && more) nas::startRunner(prog, paths.data, sync);
			restartAfter = false;
			pollStatus(true);
			if (!more && sub == STATUS) go(HOME);
			return true;
		}

		// ---- the rows -----------------------------------------------------------------------------------------------
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
			case HOME:
				if (!jobPath.empty()) add(finished ? "Last download" : "Download", jobState(true), K_JOB)->warn = !finished;
				for (size_t i = 0; i < servers.size(); i++)
				{
					Row* x = add(servers[i].title(), protoLabel(servers[i]), K_SERVER, (int)i);
					if (armed == K_SERVER && !keySeen.empty() && servers[i].id == srv.id) { x->value = "Press A again"; x->warn = true; }
				}
				add("Add a server", "", K_ADD);
				add("Archives on the card", "", K_LOCAL);
				add("Options", "", K_OPTIONS);
				break;
			case EDIT:
			{
				const fetch::ServerRec& d = draft;
				bool ftp = d.protocol == "ftp", smb = d.protocol == "smb", sftp = d.protocol == "sftp";
				add("Name", d.name.empty() ? "(the address)" : d.name, E_NAME);
				Row* p = add("Protocol", ftp ? "FTP" : sftp ? "SFTP" : "SMB", E_PROTOCOL);
				p->choice = true;
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
			case BROWSE:
				for (size_t i = 0; i < items.size(); i++)
				{
					const Item& it = items[i];
					Row* x = add(it.name, it.dir ? "Folder" : it.link ? "Link" : it.size >= 0 ? fetch::bytes(it.size) : "", B_ITEM, (int)i);
					x->mark = marked.count(it.name) > 0;
				}
				break;
			case TARGET:
			{
				const nas::Card& c = cards[std::min<size_t>((size_t)card, cards.size() - 1)];
				fetch::Space sp;
				std::string free = fetch::spaceAt(c.root, sp) ? " · " + fetch::bytes((long long)sp.free) + " free" : "";
				add("Card", c.label + free, T_CARD)->choice = cards.size() > 1;
				const nas::Folder& f = folders[std::min<size_t>((size_t)folder, folders.size() - 1)];
				add("Folder", f.label + " · " + f.rel, T_FOLDER)->choice = true;
				if (askUnpack) add("Unpack archives", unpack ? "Yes" : "No", T_UNPACK)->choice = true;
				if (askZips && unpack) add("Zips", unpackZips ? "Unpack them too" : "Keep them as they are", T_ZIPS)->choice = true;
				if (askDelete && unpack) add("Delete archives after", del ? "Yes" : "No", T_DELETE)->choice = true;
				if (existingAsked) add("Already on the card", replace ? "Replace them" : "Skip them", T_EXISTING)->choice = true;
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
			case LOCAL:
				if (lmenu)
				{
					add("Unpack here", "", L_HERE);
					add("Unpack into a folder", "", L_TO);
					// (Options' Delete the archive after on Ask: asked here, for both)
					if (option("downloads_delete_archive", "ask") == "ask") add("Then delete it", localDel ? "Yes" : "No", L_AFTER)->choice = true;
					Row* x = add("Delete it", "", L_DELETE);
					if (armed == L_DELETE) { x->value = "Press A again"; x->warn = true; }
					break;
				}
				if (ldir.empty())
				{
					for (size_t i = 0; i < cards.size(); i++)
					{
						fetch::Space sp;
						add(cards[i].label, fetch::spaceAt(cards[i].root, sp) ? fetch::bytes((long long)sp.free) + " free" : "", L_CARD, (int)i);
					}
					break;
				}
				for (size_t i = 0; i < lentries.size(); i++)
					add(lentries[i].name, lentries[i].dir ? "Folder" : fetch::bytes(lentries[i].size), L_ENTRY, (int)i);
				break;
			case OPTIONS:
			{
				auto three = [](const std::string& v) { return v == "always" ? "Always" : v == "never" ? "Never" : "Ask"; };
				add("Unpack after a download", three(option("downloads_unpack", "ask")), O_UNPACK)->choice = true;
				add("Delete the archive after", three(option("downloads_delete_archive", "ask")), O_DELETE)->choice = true;
				add("Unwrap a single folder", option("downloads_unwrap", "on") == "on" ? "On" : "Off", O_UNWRAP)->choice = true;
				add("Show hidden files", option("downloads_hidden", "off") == "on" ? "On" : "Off", O_HIDDEN)->choice = true;
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
				: "It isn't kept: Downloads asks for it again next time (the server's Remember password is off).");
		}

		void list(const std::string& path)
		{
			listing = fetch::cleanPath(path);
			if (ask(LIST, { "list", listing })) message = "Listing " + listing + "...";
		}

		void answered(const std::vector<std::vector<std::string>>& rows, const std::vector<std::string>& last)
		{
			Ask what = asked;
			asked = NOTHING;
			bool ok = !last.empty() && last[0] != "err";
			std::string word = last.size() > 1 ? last[1] : "", text = last.size() > 2 ? last[2] : "";
			// (banana-ui's log: what was asked and what went wrong, never a password)
			static const char* const ASKED[] = { "-", "connect", "list", "walk", "inspect", "put", "delete", "trust", "target", "reconnect" };
			if (!ok) fprintf(stderr, "downloads: %s: %s: %s\n", ASKED[what], word.c_str(), text.c_str());
			switch (what)
			{
			case CONNECT:
				if (ok)
				{
					message.clear();
					home = last.size() > 2 && !last[2].empty() ? last[2] : "/";
					fprintf(stderr, "downloads: connected to %s (%s), home %s\n", srv.title().c_str(), protoLabel(srv).c_str(), home.c_str());
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
					armed = K_SERVER;
					return;
				}
				message = text;
				return;
			case LIST:
				if (!ok)
				{
					if (!linkTried.empty())
					{
						// a link that doesn't list is a file: download it
						Item it;
						it.name = linkTried;
						it.size = -1;
						linkTried.clear();
						startTarget({ it });
						return;
					}
					message = text;
					if (sub != BROWSE)
					{
						// the start folder didn't open: the page on it, empty, B goes up from there
						dir = listing;
						items.clear();
						marked.clear();
						sel[BROWSE] = top[BROWSE] = 0;
						go(BROWSE);
					}
					return;
				}
				{
					bool hidden = option("downloads_hidden", "off") == "on";
					std::string was = dir;
					if (sub == BROWSE && !was.empty()) cursor[was] = { sel[BROWSE], top[BROWSE] };
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
					fprintf(stderr, "downloads: %s: %zu entries\n", dir.c_str(), items.size());
					auto c = cursor.find(dir);
					sel[BROWSE] = c == cursor.end() ? 0 : std::min(c->second.first, std::max(0, (int)items.size() - 1));
					top[BROWSE] = c == cursor.end() ? 0 : std::min(c->second.second, sel[BROWSE]);
					message.clear();
					go(BROWSE);
				}
				return;
			case WALK:
				if (!ok) { message = text; return; }
				files.clear();
				for (auto& f : rows)
					if (f.size() >= 4 && f[0] == "f") files.push_back({ atoll(f[1].c_str()), f[2], f[3] });
				walked = true;
				existing = 0;
				for (auto& f : files) if (isFile(f.local)) existing++;
				if (existing > 0 && !existingAsked)
				{
					existingAsked = true;
					message = plural(existing, "file", "files") + " of " + std::to_string(files.size()) + (existing == 1 ? " is" : " are") +
						" on the card already: skip them or replace them, then Download.";
					return;
				}
				download();
				return;
			case INSPECT:
				if (!ok)
				{
					if (word == "password" || word == "wrong_password")
					{
						keyboard(F_ARCHIVE, "Password for", fetch::baseName(archive), "",
							word == "wrong_password" ? "That password is wrong: type it again." : "The archive is protected by a password.");
						if (word == "wrong_password") ui->message = "That password is wrong: type it again.";
						return;
					}
					message = text;
					pendingAction = -1;
					return;
				}
				{
					// files, bytes, the largest, the dictionary, encrypted, the memory free
					int64_t dict = last.size() > 4 ? atoll(last[4].c_str()) : 0, mem = last.size() > 6 ? atoll(last[6].c_str()) : -1;
					message = plural(atoll(last[1].c_str()), "file", "files") + ", " + fetch::bytes(atoll(last[2].c_str())) + " unpacked.";
					if (dict > 0 && mem >= 0 && dict + (64 << 20) > mem && !ignoreMemory)
					{
						message = "Unpacking it takes " + fetch::bytes(dict) + " of memory, and " + fetch::bytes(mem) +
							" is free: it may fail. Press A again to try anyway.";
						armed = pendingAction;
						return;
					}
					unpackChosen();
				}
				return;
			case PUT:
				if (!ok) { message = text; putPassword.clear(); putTyped = false; return; }
				if (putTyped && last.size() > 1) typed[last[1]] = putPassword;
				putPassword.clear();
				putTyped = false;
				loadServers();
				for (size_t i = 0; i < servers.size(); i++)
					if (last.size() > 1 && servers[i].id == last[1]) sel[HOME] = (int)i + (jobPath.empty() ? 0 : 1);
				fprintf(stderr, "downloads: server %s saved\n", last.size() > 1 ? last[1].c_str() : "?");
				message = "Saved.";
				go(HOME);
				return;
			case FORGET:
				loadServers();
				sel[HOME] = 0;
				top[HOME] = 0;
				message = "Deleted.";
				go(HOME);
				return;
			case TRUST:
				connect(srv);
				return;
			case RECONNECT:
				// (ok: the request behind it goes now; else it is dropped, and the login says why)
				if (ok) return;
				queuedWhat = NOTHING;
				queued.clear();
				if (word == "password" || word == "auth") { typed.erase(srv.id); askLogin(); return; }
				message = text;
				return;
			default:
				return;
			}
		}

		// ---- target: where a download (or an unpacking) goes ----------------------------------------------------------
		int folderIndex(const std::string& rel)
		{
			for (size_t i = 0; i < folders.size(); i++) if (folders[i].rel == rel) return (int)i;
			return -1;
		}

		// what the chosen entries may hold: zips, and other archives (7z, RAR; a folder may hold either)
		void archivesIn(bool& zips, bool& others) const
		{
			zips = others = false;
			for (auto& it : chosen)
			{
				bool any = it.dir || it.link;
				fetch::Kind k = any ? fetch::NONE : fetch::kindOf(it.name);
				if (any || k == fetch::ZIP) zips = true;
				if (any || (k != fetch::NONE && k != fetch::ZIP)) others = true;
			}
		}

		// the unpacking rows as Options has them. Zips for a system whose emulator plays them zipped are kept as they are
		// unless asked: by Unpack archives itself when the choice is zips alone, else by a row of their own (7z and RAR
		// archives, which it doesn't play, are unpacked all the same)
		void defaults()
		{
			std::string mode = option("downloads_unpack", "ask"), delMode = option("downloads_delete_archive", "ask");
			bool zips, others;
			archivesIn(zips, others);
			bool plays = folders[folder].playsZip;
			if (local) { askUnpack = askZips = false; unpack = true; }
			else
			{
				askUnpack = mode == "ask" && (zips || others);
				unpack = mode == "always" ? true : mode == "never" ? false : others || !plays;
				askZips = askUnpack && plays && zips && others;
			}
			unpackZips = false;
			askDelete = delMode == "ask" && (zips || others || local);
			del = delMode == "always" || (local && delMode == "ask" && localDel);
		}

		// the job's zips left as they are: the folder's system plays them zipped, and nobody asked for them unpacked
		bool keepZips() const
		{
			bool zips, others;
			archivesIn(zips, others);
			return !local && folders[folder].playsZip && (askZips ? !unpackZips : !(askUnpack && !others));
		}

		void startTarget(const std::vector<Item>& what)
		{
			local = false;
			chosen = what;
			chosenDir = dir;
			walked = existingAsked = replace = false;
			files.clear();
			card = 0;
			folder = folderIndex("roms");
			// where this folder's downloads went last time, else where its first file belongs
			std::string last = srv.target(chosenDir);
			size_t colon = last.find(':');
			bool remembered = false;
			if (colon != std::string::npos)
			{
				int f = folderIndex(last.substr(colon + 1));
				for (size_t i = 0; i < cards.size(); i++) if (cards[i].id == last.substr(0, colon)) card = (int)i;
				if (f >= 0) { folder = f; remembered = true; }
			}
			if (!remembered && !chosen.empty())
			{
				const Item& it = chosen[0];
				int g = nas::suggest(folders, it.dir ? "" : it.name, it.dir ? fetch::joinPath(chosenDir, it.name) : chosenDir);
				if (g >= 0) folder = g;
			}
			if (folder < 0) folder = 0;
			defaults();
			sel[TARGET] = top[TARGET] = 0;
			message.clear();
			go(TARGET);
		}

		std::string targetDir() const { return cards[card].root + "/" + folders[folder].rel; }

		void walk()
		{
			const nas::Folder& f = folders[folder];
			std::string merge;
			for (auto& n : f.names) merge += (merge.empty() ? "" : ",") + n;
			std::vector<std::string> req = { "walk", chosenDir, targetDir(), option("downloads_hidden", "off") == "on" ? "1" : "0", merge };
			for (auto& it : chosen)
				req.push_back(std::string(it.dir ? "d" : it.link ? "l" : "f") + ":" + (it.dir || it.link ? "" : std::to_string(it.size)) + ":" + it.name);
			if (ask(WALK, req)) message = "Listing the files...";
		}

		void download()
		{
			std::vector<File> todo;
			int64_t need = 1 << 20, largest = 0;
			for (auto& f : files)
			{
				if (!replace && isFile(f.local)) continue;
				todo.push_back(f);
				if (f.size > 0) { need += f.size - std::min(f.size, sizeOf(f.local + ".part")); largest = std::max(largest, f.size); }
			}
			if (todo.empty()) { message = "Everything chosen is on the card already."; return; }
			fetch::Space sp;
			if (fetch::spaceAt(targetDir(), sp))
			{
				if ((int64_t)sp.free < need) { message = "Not enough room on " + cards[card].label + ": " + fetch::bytes(need) + " needed, " + fetch::bytes((long long)sp.free) + " free."; return; }
				if (sp.fat32 && (uint64_t)largest > fetch::FAT32_MAX) { message = "A file is " + fetch::bytes(largest) + ": this card (FAT32) takes 4 GB a file at most."; return; }
			}
			fetch::Job j;
			j.title = chosen.size() == 1 ? chosen[0].name : plural((long long)todo.size(), "file", "files") + " from " + srv.title();
			j.server = srv.id;
			j.unpack = unpack;
			j.keepZips = unpack && keepZips();
			j.deleteArchive = unpack && del;
			j.unwrap = option("downloads_unwrap", "on") == "on";
			j.tempRoot = cards[card].root;
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
			fprintf(stderr, "downloads: %s: %zu files from %s into %s\n", path.c_str(), todo.size(), srv.title().c_str(), targetDir().c_str());
			// a password typed for this session goes to the runner, in a file only it reads (once)
			auto t = typed.find(srv.id);
			if (!srv.guest && t != typed.end()) nas::writeSecret(paths.password(srv.id), t->second);
			// where this folder's downloads went: the same next time
			std::string where = cards[card].id + ":" + folders[folder].rel;
			if (srv.target(chosenDir) != where)
			{
				// (the list's copy too: the server opened again this time goes to the same place)
				for (fetch::ServerRec* s : { &srv, serverById(srv.id) })
				{
					if (!s) continue;
					bool had = false;
					for (auto& t : s->targets) if (t.first == chosenDir) { t.second = where; had = true; }
					if (!had) s->targets.push_back({ chosenDir, where });
				}
				ask(REMEMBER, { "target", srv.id, chosenDir, where });
			}
			if (!running()) nas::startRunner(prog, paths.data, sync);
			marked.clear();
			message.clear();
			pollStatus(true);
			sel[STATUS] = top[STATUS] = 0;
			sel[HOME] = top[HOME] = 0;	// (B comes back to the job's row)
			go(STATUS);
		}

		// ---- local archives ------------------------------------------------------------------------------------------
		void openLocal(const std::string& d)
		{
			if (!ldir.empty()) cursor["local:" + ldir] = { sel[LOCAL], top[LOCAL] };
			ldir = d;
			lmenu = false;
			lentries = d.empty() ? std::vector<nas::LocalEntry>() : nas::localEntries(d);
			auto c = cursor.find("local:" + d);
			int n = d.empty() ? (int)cards.size() : (int)lentries.size();
			sel[LOCAL] = c == cursor.end() ? 0 : std::min(c->second.first, std::max(0, n - 1));
			top[LOCAL] = c == cursor.end() ? 0 : std::min(c->second.second, sel[LOCAL]);
			go(LOCAL);
		}

		// the archive in hand looked at (a password asked when it has one), then unpacked here or into a folder
		void inspect(int action)
		{
			pendingAction = action;
			std::vector<std::string> req = { "inspect", archive };
			if (!archivePassword.empty()) req.push_back(archivePassword);
			if (ask(INSPECT, req)) message = "Looking into " + fetch::baseName(archive) + "...";
		}

		void unpackChosen()
		{
			int action = pendingAction;
			pendingAction = -1;
			if (action == L_TO)
			{
				local = true;
				chosen = { Item() };
				chosen[0].name = fetch::baseName(archive);
				chosenDir = ldir;
				existingAsked = false;
				for (size_t i = 0; i < cards.size(); i++)
					if (archive.compare(0, cards[i].root.size() + 1, cards[i].root + "/") == 0) card = (int)i;
				int g = nas::suggest(folders, chosen[0].name, "");
				folder = g >= 0 ? g : folderIndex("roms");
				defaults();
				sel[TARGET] = top[TARGET] = 0;
				go(TARGET);
				return;
			}
			std::string root = ldir, delMode = option("downloads_delete_archive", "ask");
			for (auto& c : cards) if (archive.compare(0, c.root.size() + 1, c.root + "/") == 0) root = c.root;
			extractJob(ldir, root, delMode == "always" || (delMode == "ask" && localDel));
		}

		void extractJob(const std::string& dest, const std::string& root, bool deleteAfter)
		{
			fetch::Job j;
			j.title = "Unpack " + fetch::baseName(archive);
			j.unpack = true;
			j.deleteArchive = deleteAfter;
			j.unwrap = option("downloads_unwrap", "on") == "on";
			j.ignoreMemory = ignoreMemory;
			j.tempRoot = root;
			fetch::JobItem it;
			it.extract = true;
			it.remote = archive;
			it.local = dest;
			j.items.push_back(it);
			std::string path = nas::newJobPath(paths);
			if (!archivePassword.empty())
			{
				j.archivePasswordFile = paths.run + "/fetch-" + path.substr(path.rfind('/') + 1) + ".apw";
				nas::writeSecret(j.archivePasswordFile, archivePassword);
			}
			if (!fetch::writeJob(path, j)) { message = "Couldn't write the job in " + paths.jobs() + "."; return; }
			fprintf(stderr, "downloads: %s: %s unpacked into %s\n", path.c_str(), archive.c_str(), dest.c_str());
			archivePassword.clear();
			ignoreMemory = false;
			lmenu = false;
			if (!running()) nas::startRunner(prog, paths.data, sync);
			message.clear();
			pollStatus(true);
			sel[STATUS] = top[STATUS] = 0;
			sel[HOME] = top[HOME] = 0;	// (B comes back to the job's row)
			go(STATUS);
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
				const char* key = kind == O_UNPACK ? "downloads_unpack" : "downloads_delete_archive";
				std::string v = option(key, "ask");
				int at = v == "always" ? 1 : v == "never" ? 2 : 0;
				setConfValue(paths.data + "/settings.conf", key, V[(at + d + 3) % 3]);
				break;
			}
			case O_UNWRAP: setConfValue(paths.data + "/settings.conf", "downloads_unwrap", option("downloads_unwrap", "on") == "on" ? "off" : "on"); break;
			case O_HIDDEN: setConfValue(paths.data + "/settings.conf", "downloads_hidden", option("downloads_hidden", "off") == "on" ? "off" : "on"); break;
			case T_CARD: card = (card + d + (int)cards.size()) % (int)cards.size(); walked = existingAsked = false; break;
			case T_FOLDER:
				folder = (folder + d + (int)folders.size()) % (int)folders.size();
				walked = existingAsked = false;
				if (askUnpack) defaults();
				break;
			case T_UNPACK: unpack = !unpack; break;
			case T_ZIPS: unpackZips = !unpackZips; break;
			case T_DELETE: del = !del; break;
			case L_AFTER: localDel = !localDel; break;
			case T_EXISTING: replace = !replace; break;
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
				if (armed >= 0 && armed != K_SERVER) armed = -1;
				if (armed == K_SERVER && keySeen.empty()) armed = -1;
				return MOVED;
			};
			switch (k)
			{
			case Key::UP: return moveTo(s - 1);
			case Key::DOWN: return moveTo(s + 1);
			case Key::L: if (!n) return NONE; s = std::max(0, s - ROWS); t = std::max(0, t - ROWS); if (s < t) t = s; return MOVED;
			case Key::R: if (!n) return NONE; s = std::min(n - 1, s + ROWS); if (s >= t + ROWS) t = s - ROWS + 1; return MOVED;
			case Key::START:
				if (sub == EDIT) { save(); return MOVED; }
				if (sub == TARGET) return activate(T_GO, nullptr);
				ui->close();
				return CLOSED;
			case Key::B: return back();
			case Key::LEFT: case Key::RIGHT:
				if (cur && cur->choice) { change(cur->kind, k == Key::LEFT ? -1 : 1); armed = -1; return CHANGED; }
				if (k == Key::LEFT && sub != HOME) return back();
				return NONE;
			case Key::A: return cur ? activate(cur->kind, cur) : (sub == STATUS ? activate(S_ITEM, nullptr) : NONE);
			case Key::X:
				if (sub == BROWSE) return downloadMarked(cur);
				if (sub == STATUS && running() && !finished && cancelling.empty())
				{
					if (armed != S_ITEM) { armed = S_ITEM; message = "Press X again to stop. The files downloaded so far are kept, and Resume goes on."; return MOVED; }
					armed = -1;
					stop();
					return CHANGED;
				}
				return NONE;
			case Key::Y:
				if (sub == STATUS && canCancel())
				{
					if (armed != S_CANCEL)
					{
						armed = S_CANCEL;
						message = "Press Y again to cancel this download: what it hasn't finished is deleted (the files it finished stay on the card).";
						return MOVED;
					}
					armed = -1;
					cancel();
					return CHANGED;
				}
				if (sub == HOME && cur && cur->kind == K_SERVER) { edit(&servers[cur->index]); return MOVED; }
				if (sub == BROWSE && cur)
				{
					const Item& it = items[cur->index];
					if (marked.count(it.name)) marked.erase(it.name); else marked.insert(it.name);
					return MOVED;
				}
				return NONE;
			case Key::SELECT:
				if (sub == BROWSE) { list(dir); return MOVED; }
				if (sub == LOCAL) { openLocal(ldir); return MOVED; }
				return NONE;
			default:
				return NONE;
			}
		}

		Result downloadMarked(const Row* cur)
		{
			std::vector<Item> what;
			for (auto& it : items) if (marked.count(it.name)) what.push_back(it);
			if (what.empty() && cur) what.push_back(items[cur->index]);
			if (what.empty()) return NONE;
			startTarget(what);
			return MOVED;
		}

		Result activate(int kind, const Row* cur)
		{
			switch (kind)
			{
			case K_JOB: go(STATUS); sel[STATUS] = top[STATUS] = 0; pollStatus(true); return MOVED;
			case K_SERVER:
			{
				const fetch::ServerRec& s = servers[cur->index];
				if (armed == K_SERVER && !keySeen.empty() && srv.id == s.id)
				{
					armed = -1;
					ask(TRUST, { "trust", s.id, keySeen });
					message = "Trusting the new key...";
					return MOVED;
				}
				armed = -1;
				connect(s);
				return MOVED;
			}
			case K_ADD: edit(nullptr); return MOVED;
			case K_LOCAL: message.clear(); openLocal(""); return MOVED;
			case K_OPTIONS: message.clear(); sel[OPTIONS] = top[OPTIONS] = 0; go(OPTIONS); return MOVED;
			case E_NAME: keyboard(F_NAME, "Server", "Its name", draft.name, fieldHelp(E_NAME)); return MOVED;
			case E_HOST: keyboard(F_HOST, "Server", "Its address", draft.host, fieldHelp(E_HOST)); return MOVED;
			case E_PORT: keyboard(F_PORT, "Server", "Its port", std::to_string(draft.port > 0 ? draft.port : draft.defaultPort()), fieldHelp(E_PORT)); return MOVED;
			case E_USER: keyboard(F_USER, "Server", "The user", draft.user, fieldHelp(E_USER)); return MOVED;
			case E_PASSWORD: keyboard(F_PASSWORD, "Server", "The password", "", fieldHelp(E_PASSWORD)); return MOVED;
			case E_DOMAIN: keyboard(F_DOMAIN, "Server", "Its domain", draft.domain, fieldHelp(E_DOMAIN)); return MOVED;
			case E_START: keyboard(F_START, "Server", "The start folder", draft.start, fieldHelp(E_START)); return MOVED;
			case E_PROTOCOL: case E_GUEST: case E_REMEMBER: case E_TLS: case E_ENCODING:
			case O_UNPACK: case O_DELETE: case O_UNWRAP: case O_HIDDEN:
			case T_CARD: case T_FOLDER: case T_UNPACK: case T_ZIPS: case T_DELETE: case T_EXISTING: case L_AFTER:
				change(kind, 1);
				return CHANGED;
			case E_SAVE: save(); return MOVED;
			case E_DELETE:
				if (armed != E_DELETE) { armed = E_DELETE; message = draft.title() + " goes from the list (its password too). Press A again."; return MOVED; }
				armed = -1;
				ask(FORGET, { "delete", draft.id });
				return MOVED;
			case B_ITEM:
			{
				const Item& it = items[cur->index];
				if (it.dir) { list(fetch::joinPath(dir, it.name)); return MOVED; }
				if (it.link) { linkTried = it.name; list(fetch::joinPath(dir, it.name)); return MOVED; }
				startTarget({ it });
				return MOVED;
			}
			case T_GO:
				if (local)
				{
					extractJob(targetDir(), cards[card].root, del);
					return MOVED;
				}
				if (!walked) { walk(); return MOVED; }
				download();
				return MOVED;
			case S_ITEM:
			{
				// a finished job: A clears it; one stopped or waiting: A goes on; an archive left packed: unpacked from the card
				if (!cancelling.empty()) return NONE;	// (a job being cancelled isn't resumed)
				const fetch::Outcome* o = cur ? outcome(cur->index) : nullptr;
				if (o && o->what == "packed" && cur->index < (int)job.items.size())
				{
					const fetch::JobItem& it = job.items[cur->index];
					std::string first, key;
					archive = it.extract ? it.remote : fetch::volumeOf(it.local, first, key) ? first : it.local;
					if (isFile(archive))
					{
						archivePassword.clear();
						localDel = false;
						openLocal(fetch::parentPath(archive));
						lmenu = true;
						sel[LOCAL] = top[LOCAL] = 0;
						return MOVED;
					}
				}
				if (finished)
				{
					unlink(jobPath.c_str());
					unlink((jobPath + ".done").c_str());
					pollStatus(true);
					if (jobPath.empty() || finished) go(HOME);
					return MOVED;
				}
				if (!running()) { resume(); return MOVED; }
				return NONE;
			}
			case L_CARD: openLocal(cards[cur->index].root); return MOVED;
			case L_ENTRY:
			{
				const nas::LocalEntry& e = lentries[cur->index];
				if (e.dir) { openLocal(ldir + "/" + e.name); return MOVED; }
				archive = ldir + "/" + e.name;
				archivePassword.clear();
				ignoreMemory = false;
				localDel = false;
				message.clear();
				lmenu = true;
				sel[LOCAL] = top[LOCAL] = 0;
				return MOVED;
			}
			case L_HERE: case L_TO:
				if (armed == kind) { armed = -1; ignoreMemory = true; pendingAction = kind; unpackChosen(); return MOVED; }
				inspect(kind);
				return MOVED;
			case L_DELETE:
				if (armed != L_DELETE) { armed = L_DELETE; message = fetch::baseName(archive) + " is deleted from the card (all its parts). Press A again."; return MOVED; }
				armed = -1;
				for (auto& v : fetch::volumes(archive)) unlink(v.c_str());
				message = "Deleted.";
				openLocal(ldir);
				return MOVED;
			default:
				return NONE;
			}
		}

		Result back()
		{
			armed = -1;
			if (waiting()) { giveUp(); return MOVED; }	// (a slow server: B gives up on it)
			switch (sub)
			{
			case HOME:
				ui->close();
				return CLOSED;
			case BROWSE:
				if (dir == "/" || dir.empty()) { message.clear(); go(HOME); return MOVED; }
				cursor[dir] = { sel[BROWSE], top[BROWSE] };
				list(fetch::parentPath(dir));
				return MOVED;
			case TARGET:
				message.clear();
				go(local ? LOCAL : BROWSE);
				return MOVED;
			case LOCAL:
				if (lmenu) { lmenu = false; message.clear(); openLocal(ldir); return MOVED; }
				if (ldir.empty()) { go(HOME); return MOVED; }
				for (auto& c : cards) if (c.root == ldir) { openLocal(""); return MOVED; }
				openLocal(fetch::parentPath(ldir));
				return MOVED;
			default:
				message.clear();
				go(HOME);
				return MOVED;
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
			int& s = sel[sub];
			int& t = top[sub];
			for (int i = 0; i < ROWS && t + i < (int)r.size(); i++)
			{
				float ry = 82 + i * 64;
				if (dy < ry || dy > ry + 56) continue;
				int at = t + i;
				// browse: the tick's place marks and unmarks
				if (sub == BROWSE && dx < 70) { s = at; return press(Key::Y); }
				if (at != s) { s = at; if (armed >= 0 && armed != K_SERVER) armed = -1; return MOVED; }
				if (r[at].choice) return press(dx < 320 ? Key::LEFT : Key::RIGHT);
				return press(Key::A);
			}
			if (std::fabs(cy - dy) > 40 && !r.empty())
			{
				int n = (int)r.size();
				t = std::max(0, std::min(std::max(0, n - ROWS), t + (cy < dy ? ROWS : -ROWS)));
				s = std::max(t, std::min(s, t + ROWS - 1));
				return MOVED;
			}
			return NONE;
		}

		// ---- the keyboard's text --------------------------------------------------------------------------------------
		Result entered(const std::string& text)
		{
			Field f = field;
			field = F_NONE;
			ui->page = DOWNLOADS;
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
				inspect(pendingAction >= 0 ? pendingAction : L_HERE);
				return MOVED;
			default: break;
			}
			return CHANGED;
		}

		// ---- what the top panel says ------------------------------------------------------------------------------
		struct Top { std::string icon = "downloads", label = "Downloads", title, accent, help; bool meters = false; double file = 0, all = 0; std::string fileLabel; };

		Top topPanel(const std::vector<Row>& r)
		{
			Top t;
			const Row* cur = sel[sub] < (int)r.size() ? &r[sel[sub]] : nullptr;
			switch (sub)
			{
			case HOME:
				if (!cur) break;
				t.title = cur->label;
				if (cur->kind == K_JOB) { t.title = job.title; t.accent = jobState(false); t.help = "A shows each file. The download carries on while you play or the menu shows anything else; with the lid closed it finishes before the handheld sleeps."; }
				else if (cur->kind == K_SERVER)
				{
					const fetch::ServerRec& s = servers[cur->index];
					t.accent = address(s);
					t.help = "A opens its folders, Y changes it or deletes it.";
					if (s.protocol == "ftp" && !s.tls && !s.guest) t.help += " Plain FTP sends the password unencrypted.";
				}
				else if (cur->kind == K_ADD) t.help = "A NAS, or a computer sharing a folder: over SMB (Windows' and macOS's file sharing, most NAS), SFTP (SSH) or FTP. Games go straight into their system's folder on the card, archives unpacked.";
				else if (cur->kind == K_LOCAL) t.help = "Archives already on the cards (.zip, .7z, .rar): unpacked where they are or into a system's folder, or deleted.";
				else t.help = "Unpacking after a download, the archive deleted after, an archive's single folder unwrapped, hidden files.";
				break;
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
				t.title = dir;
				if (cur)
				{
					const Item& it = items[cur->index];
					t.title = it.name;
					t.accent = it.dir ? "Folder" : it.link ? "Link" : fetch::bytes(it.size) + (it.mtime ? " · " + date(it.mtime) : "");
					int g = nas::suggest(folders, it.dir ? "" : it.name, it.dir ? fetch::joinPath(dir, it.name) : dir);
					std::string last = srv.target(dir);
					std::string goes = !last.empty() ? last.substr(last.find(':') + 1) : g >= 0 ? folders[g].label + " (" + folders[g].rel + ")" : "";
					if (!goes.empty()) t.help = "Goes to " + goes + ". ";
				}
				int64_t bytes = 0;
				for (auto& it : items) if (marked.count(it.name) && !it.dir && it.size > 0) bytes += it.size;
				if (!marked.empty()) t.help += plural((long long)marked.size(), "item", "items") + " marked" + (bytes ? ", " + fetch::bytes(bytes) : "") + ". ";
				t.help += "A opens a folder or downloads a file. Y marks, X downloads what is marked (a folder with all in it). SELECT reads the folder again, B goes up.";
				break;
			}
			case TARGET:
				t.label = local ? "Unpack" : "Download";
				t.title = chosen.size() == 1 ? chosen[0].name : plural((long long)chosen.size(), "item", "items");
				t.accent = targetDir();
				if (walked)
				{
					int64_t b = 0;
					for (auto& f : files) if (f.size > 0) b += f.size;
					t.accent = plural((long long)files.size(), "file", "files") + ", " + fetch::bytes(b);
				}
				t.help = local ? "Into the folder chosen. Archives of one folder are unwrapped (Options)."
					: "Into the folder chosen (the server's folder of the same system is merged into it). Archives are unpacked after their last part comes in.";
				if (cur && cur->kind == T_UNPACK)
					t.help = askZips ? "Yes: 7z and RAR archives unpacked into the folder, then (Delete archives after) deleted. This system plays its games zipped: its zips are kept as they are unless the next row says otherwise."
						: "Yes: archives unpacked into the folder, then (Delete archives after) deleted. Zips for a system that plays them zipped are kept as they are by default.";
				if (cur && cur->kind == T_ZIPS) t.help = "This system plays its games zipped, so its zips are kept as they are, the 7z and RAR archives unpacked all the same. Unpack them too: the zips as well.";
				break;
			case STATUS:
			{
				t.label = "Downloads";
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
						t.help = cur->label + ": " + d + (o->what == "packed" ? " A unpacks it from the card." : "");
						break;
					}
				}
				t.help = finished ? "A clears it (the files stay on the card). Your games show in the menu when you go back to it."
					: !cancelling.empty() ? "The download stops, then what it hasn't finished is deleted."
					: st.state == "need" ? "A types the password. Y cancels the download."
					: !running() ? "A goes on from where it stopped. Y cancels it: what it hasn't finished is deleted."
					: "B goes back: it carries on meanwhile. X stops it, Y cancels it.";
				if (jobsBehind > 0) t.help += " " + plural(jobsBehind, "more job waits", "more jobs wait") + " after this one.";
				break;
			}
			case LOCAL:
				t.label = "Archives on the card";
				if (lmenu)
				{
					t.title = fetch::baseName(archive);
					t.accent = fetch::bytes(sizeOf(archive));
					t.help = cur && cur->kind == L_TO ? "Into a system's folder on a card (chosen next)."
						: cur && cur->kind == L_AFTER ? "Yes: once it is unpacked, the archive is deleted (all its parts). Options sets it for every archive."
						: cur && cur->kind == L_DELETE ? "Deleted from the card, all its parts." : "Unpacked into the folder it is in.";
					break;
				}
				t.title = ldir.empty() ? "The cards" : ldir;
				if (cur && cur->kind == L_ENTRY)
				{
					const nas::LocalEntry& e = lentries[cur->index];
					t.title = e.name;
					t.accent = e.dir ? "Folder" : fetch::bytes(e.size);
				}
				t.help = "A opens a folder, or an archive's choices: unpacked here or into a system's folder, or deleted. B goes up.";
				break;
			case OPTIONS:
				t.label = "Downloads";
				t.title = cur ? cur->label : "";
				t.accent = cur ? cur->value : "";
				if (cur)
					switch (cur->kind)
					{
					case O_UNPACK: t.help = "Ask: Download's page asks each time. Always or Never: it doesn't ask."; break;
					case O_DELETE: t.help = "An archive unpacked is deleted after (all its parts): ask each time, always or never."; break;
					case O_UNWRAP: t.help = "An archive holding a single folder: its contents go into the folder chosen, not the folder itself."; break;
					default: t.help = "Files and folders whose names start with a dot, on the servers."; break;
					}
				break;
			default:
				break;
			}
			if (!message.empty()) t.help = message;
			return t;
		}

		std::string header(int n)
		{
			switch (sub)
			{
			case HOME: return "Downloads";
			case EDIT: return existingServer ? "Change the server" : "A new server";
			case BROWSE: return srv.title() + ": " + dir;
			case TARGET: return local ? "Unpack where" : "Download where";
			case STATUS: return finished ? "Last download" : "Downloading";
			case LOCAL: return lmenu ? fetch::baseName(archive) : ldir.empty() ? "Archives on the card" : ldir;
			case OPTIONS: return "Options";
			default: (void)n; return "";
			}
		}

		std::string emptyText()
		{
			if (waiting()) return "...";
			if (sub == BROWSE) return "An empty folder.";
			if (sub == LOCAL) return "No archives or folders here.";
			if (sub == STATUS) return "Nothing to download.";
			return "";
		}

		// ---- drawing ------------------------------------------------------------------------------------------------
		void render()
		{
			SPal p = pal(ui->light);
			std::vector<Row> r = rows();
			Top t = topPanel(r);
			rnds::Backend* be = ui->be;
			be->beginScreen(0);
			ui->box("screen", false, 0, 0, 640, 480);
			ui->box("hero", false, 18, 20, 604, 436);
			ui->icon(t.icon, 50, 56, 3);
			ui->text(ui->fit(t.label, true, 17, 420), true, 17, 170, 62, p.muted, true, 1.4f);
			ui->text(ui->fit(t.title.empty() ? " " : t.title, true, 30, 420), true, 30, 170, 88, p.ink);
			if (!t.accent.empty()) ui->text(ui->fit(t.accent, true, 22, 420), true, 22, 170, 130, p.accent);
			ui->wrapped(t.help, 20, 50, 196, 540, t.meters ? 4 : 10, message.empty() ? p.muted : p.ink);
			if (t.meters)
			{
				ui->meter(50, 316, 540, t.file);
				ui->text(ui->fit(t.fileLabel, false, 16, 540), false, 16, 50, 338, p.muted);
				ui->meter(50, 376, 540, t.all);
				ui->text(plural((long long)outcomes.size(), "file done", "files done") + " of " + std::to_string(st.n), false, 16, 50, 398, p.muted);
			}
			be->endScreen();

			be->beginScreen(1);
			ui->box("screen", false, 0, 0, 640, 480);
			ui->box("bubble", false, 18, 14, 604, 54);
			std::string count = r.empty() ? "" : std::to_string(sel[sub] + 1) + " / " + std::to_string(r.size());
			if (sub == BROWSE && !marked.empty()) count = std::to_string(marked.size()) + " marked";
			float cw = ui->text(count, true, 17, 0, 0, p.muted, false, 0, true);
			ui->text(ui->fit(header((int)r.size()), true, 22, 540 - cw), true, 22, 38, 28, p.ink);
			ui->text(count, true, 17, 602 - cw, 31, p.muted);
			int& s = sel[sub];
			int& tp = top[sub];
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
				float vw = row.value.empty() ? 0 : ui->text(row.value, true, 19, 0, 0, vc, false, 0, true);
				float lx = 38;
				if (row.mark) { ui->tick(38, y + 21, row.warn ? vc : (on ? p.onPick : p.accent)); lx = 62; }
				float room = (row.choice ? 556 - vw - 16 : 586 - vw) - lx - 16;
				ui->text(ui->fit(row.label, true, 20, room), true, 20, lx, y + 16, c);
				if (row.value.empty()) continue;
				std::string v = ui->fit(row.value, true, 19, row.choice ? 300 : 330);
				vw = ui->text(v, true, 19, 0, 0, vc, false, 0, true);
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
			std::vector<Settings::Chip> cs = { { Key::B, "B", sub == HOME ? "Close" : sub == BROWSE ? "Up" : "Back" } };
			std::string a;
			switch (sub)
			{
			case HOME:
				a = !cur ? "" : cur->kind == K_SERVER ? (armed == K_SERVER ? "Trust" : "Connect") : "Open";
				if (cur && cur->kind == K_SERVER) cs.push_back({ Key::Y, "Y", "Edit" });
				break;
			case EDIT:
				a = !cur ? "" : cur->kind == E_SAVE ? "Save" : cur->kind == E_DELETE ? "Delete" : cur->choice ? "Change" : "Type";
				if (cur && cur->kind != E_SAVE) cs.push_back({ Key::START, "START", "Save" });
				break;
			case BROWSE:
				a = !cur ? "" : items[cur->index].dir ? "Open" : "Download";
				if (cur) cs.push_back({ Key::Y, "Y", marked.count(items[cur->index].name) ? "Unmark" : "Mark" });
				if (cur) cs.push_back({ Key::X, "X", "Get" });
				break;
			case TARGET: a = !cur ? "" : cur->kind == T_GO ? (local ? "Unpack" : "Download") : "Change"; break;
			case STATUS:
				a = !cancelling.empty() ? "" : finished ? "Clear" : st.state == "need" ? "Password" : !running() && !jobPath.empty() ? "Resume" : "";
				if (cancelling.empty() && cur && cur->index >= 0 && outcome(cur->index) && outcome(cur->index)->what == "packed") a = "Unpack";
				if (!finished && running() && cancelling.empty()) cs.push_back({ Key::X, "X", armed == S_ITEM ? "Stop!" : "Stop" });
				if (canCancel()) cs.push_back({ Key::Y, "Y", armed == S_CANCEL ? "Cancel!" : "Cancel" });
				break;
			case LOCAL:
				a = !cur ? "" : cur->kind == L_DELETE ? "Delete" : cur->kind == L_HERE || cur->kind == L_TO ? "Unpack" : cur->choice ? "Change" : "Open";
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
			if ((sub == STATUS || sub == HOME) && pollStatus(false)) changed = true;
			(void)t;
			return changed;
		}

		int wake(double t) const
		{
			if (session.answered()) return 0;	// (an answer in: taken at once)
			if (!session.busy() && queuedWhat != NOTHING) return 0;	// (the request behind it goes now)
			int w = -1;
			auto sooner = [&](int ms) { if (ms >= 0 && (w < 0 || ms < w)) w = ms; };
			if (session.busy() && !ui->wake) sooner(50);	// (no one to wake the loop when the answer is in: looked for)
			if (!cancelling.empty()) sooner(100);	// (the runner's end, which the job's deletion waits for)
			if ((sub == STATUS || sub == HOME) && !jobPath.empty() && !finished) sooner(std::max(0, (int)(stNext - t)));
			return w;
		}
	};

	// ---- Settings' side -------------------------------------------------------------------------------------------
	void Settings::openDownloads()
	{
		if (!dl)
		{
			dl = std::make_shared<Downloads>();
			dl->ui = this;
			const char* prog = getenv("BANANA_FETCH");	// (a stand-in, for the headless test)
			dl->prog = prog && *prog ? prog : "/usr/bin/banana-fetch";
			dl->paths = fetch::Paths::fromEnv(data);
		}
		dl->sync = sync;
		mOpen = true;
		page = DOWNLOADS;
		dl->open();
	}

	void Settings::closeDownloads()
	{
		if (dl) dl->close();
	}

	Settings::Result Settings::pressDownloads(Key k) { return dl ? dl->press(k) : NONE; }
	Settings::Result Settings::touchDownloads(float cx, float cy) { return dl ? dl->touch(cx, cy) : NONE; }
	void Settings::renderDownloads() { if (dl) dl->render(); }
	bool Settings::updateDownloads(double t) { return dl && dl->update(t); }
	int Settings::downloadsWake(double t) const { return dl ? dl->wake(t) : -1; }

	Settings::Result Settings::finishDownloadsEntry()
	{
		std::string text = pw;
		pw.clear();
		page = DOWNLOADS;
		message.clear();
		return dl ? dl->entered(text) : MOVED;
	}

	// a chosen file's tick, on the look's pixel grid
	void Settings::tick(float x, float y, const Color& c)
	{
		auto r = [&](float rx, float ry, float rw, float rh) { be->fill(std::round(X(x + rx)), std::round(X(y + ry)), std::round(X(rw)), std::round(X(rh)), c); };
		r(0, 6, 3, 3);
		r(3, 9, 3, 3);
		r(6, 6, 3, 3);
		r(9, 3, 3, 3);
		r(12, 0, 3, 3);
	}

	void Settings::meter(float x, float y, float w, double fraction)
	{
		SPal p = pal(light);
		fraction = std::max(0.0, std::min(1.0, fraction));
		be->fill(X(x), X(y), X(w), X(14), p.line);
		if (fraction > 0) be->fill(X(x + 2), X(y + 2), X((w - 4) * (float)fraction), X(10), p.accent);
	}

	bool Settings::downloadsToast(double t)
	{
		// the finished jobs not seen yet (the status line can't say: the next job's overwrites a "done" at once, and two
		// jobs can end the same)
		std::vector<std::string> done = fetch::finishedJobs(fetch::Paths::fromEnv(data).jobs());
		bool first = !fetchLooked;
		fetchLooked = true;
		fetch::Status st;
		int jobs = 0;
		std::string title;
		for (auto& f : done)
		{
			if (!fetchSeen.insert(f).second || first) continue;
			fetch::Job j;
			if (!fetch::readJob(f, j)) continue;
			fetch::Status s = fetch::summary(j, fetch::readOutcomes(f + ".done"));
			st.downloaded += s.downloaded;
			st.unpacked += s.unpacked;
			st.packed += s.packed;
			st.failed += s.failed;
			title = j.title;
			jobs++;
		}
		if (!jobs) return false;
		std::string what;
		auto part = [&](int n, const char* word) { if (n > 0) what += (what.empty() ? "" : ", ") + std::to_string(n) + " " + word; };
		part(st.downloaded, "downloaded");
		part(st.unpacked, "unpacked");
		part(st.packed, "left packed");
		part(st.failed, "failed");
		std::string name = jobs > 1 ? std::to_string(jobs) + " downloads" : title;
		now = t;
		toast(st.failed ? "Downloads: done, not all of it" : "Downloads: done", (name.empty() ? "" : name + ": ") + (what.empty() ? "nothing new" : what),
			st.failed ? 0xff7a4a : 0x6ad07a, st.failed ? 2 : 1, 7000);
		toastClosed = true;
		return true;
	}
}
