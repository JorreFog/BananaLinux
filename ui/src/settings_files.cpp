// banana-ui: Files, the shelf's tile. The cards' folders and files: copied, moved, renamed, deleted, folders made,
// archives unpacked, and the file servers' downloaded (as the settings' pages: a list on the bottom panel, what is
// chosen explained on the top one).
//   cards     the job in the background (a download, an unpacking), the cards (this one, the second SD card while it
//             is in), the servers, Add a server, Download options
//   folder    a folder: A opens, Y marks, X the actions, B up
//   actions   for what is marked (or the row under the cursor) and for the folder: Paste here (a server's files copied:
//             downloaded here), Copy, Move, Rename, Delete, Unpack..., New folder, Mark all, Clear the clipboard
//   clash     a paste whose names are taken: Keep both, Replace, Skip those
//   job       a copy, a move or a delete at work: its file, its progress; Y stops
// Choosing a folder for a download or an unpacking (pick) is the same navigation: Y takes the folder shown, X makes one.
// fileops.h does the work (the job on a thread of its own); the servers, the downloads and the unpacking are
// settings_servers.cpp's. This file asks and draws.
#include "settings.h"
#include "settings_draw.h"
#include "fileops.h"
#include "fetch/names.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

using rnds::Color;

namespace banana
{
	namespace
	{
		std::string plural(long long n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

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

		std::string duration(double s)
		{
			if (s < 60) return std::to_string((int)s + 1) + " s";
			if (s < 3600) return std::to_string((int)(s / 60) + 1) + " min";
			return std::to_string((int)(s / 3600)) + " h " + std::to_string((int)(s / 60) % 60) + " min";
		}

		std::string pathJoin(const std::string& dir, const std::string& name) { return !dir.empty() && dir.back() == '/' ? dir + name : dir + "/" + name; }
	}

	struct Settings::Files
	{
		enum Sub { CARDS, FOLDER, ACTIONS, CLASH, JOB, SUBS };
		// what each row does
		enum Kind { R_CARD, R_ENTRY, R_JOB, R_SERVER, R_ADD, R_OPTIONS, A_PASTE, A_COPY, A_MOVE, A_RENAME, A_DELETE, A_UNPACK, A_FOLDER,
			A_MARKALL, A_UNMARKALL, A_CLEAR, C_KEEP, C_REPLACE, C_SKIP, J_ITEM };
		// choosing a folder for the server pages (pick): a download's, an unpacking's
		enum Pick { NO_PICK, PICK_DOWNLOAD, PICK_UNPACK };

		struct Row
		{
			std::string label, value;
			int kind = 0, index = -1;
			bool mark = false;		// a tick before the label
			bool warn = false;		// the value in the accent
			int glyph = -1;			// 0 a folder, 1 a file, 2 a card, 3 a server
		};

		Settings* ui;
		bool sync = false;
		Sub sub = FOLDER;
		int sel[SUBS] = {}, top[SUBS] = {};
		std::string message;		// the top panel's word: what happened, what went wrong
		int armed = -1;				// an action waiting for its second press (its kind)
		double armedAt = 0;			// (Stop's: it lets go after a while)
		double now = 0;

		std::vector<files::Card> cards;
		std::vector<std::string> systems;	// the systems' folders under roms/ (kept where they are)
		std::string dir;					// the folder shown
		std::vector<files::Entry> entries;
		std::set<std::string> marked;
		std::map<std::string, std::pair<int, int>> cursor;	// a folder's selection and scroll, to come back to
		std::map<std::string, int> counts;					// the folders' entries, for the top panel
		// the clipboard: full paths, copied or moved, and the folder they were put in it from
		files::Op clipOp = files::Op::COPY;
		std::vector<std::string> clip;
		std::vector<bool> clipDirs;	// (which are folders, from the listing: no stat of each when it is pasted)
		bool clipGames = false;		// any of it a game (Paste here's word on play times)
		std::string clipFrom;
		// the actions: the names in dir they are for, and those of them that can be moved, renamed or deleted (not the
		// card's own folders: Mark all in roms/ marks the systems' folders too); which of them are folders
		std::vector<std::string> targets, movable;
		std::vector<bool> targetDirs, movableDirs;
		bool movableGames = false;
		std::vector<std::string> clashNames;	// a paste's names already taken, waiting for Keep both / Replace / Skip
		std::string renaming;					// the name the keyboard renames
		// the job
		files::Job job;
		bool pending = false, jobShown = false;	// a job started and not yet told; its page up
		double jobDue = 0, lastDraw = 0;			// its page once it has run this long; its last progress frame
		files::Op jobOp = files::Op::COPY;
		std::vector<std::string> jobSources;
		std::vector<bool> jobFolders;				// (which of them are folders, for their rows' glyph)
		std::string jobDir;							// where the pages go back to once it is done
		std::string jobWhere;						// jobDir as the pages said it (its card may be gone when it is told)
		bool cardsLater = false;					// a card came or went while the keyboard was up (looked at once it is down)
		// the servers (their pages are settings_servers.cpp's): the list, a server's files copied (the clipboard's, then:
		// Paste here downloads them), the .part files of the folder shown that a download still writes
		std::vector<Settings::ServerInfo> servers;
		Settings::RemoteClip rclip;
		std::set<std::string> parts;
		std::vector<std::string> unpackables;		// the archives among the actions' targets (full paths)
		Pick pick = NO_PICK;
		std::string pickWhat;						// what the folder is chosen for ("3 files from NAS")

		// ---- the basics ---------------------------------------------------------------------------------------------
		void log(const std::string& m) { if (ui->log) ui->log("files: " + m); }

		const files::Card* cardOf(const std::string& path) const
		{
			const files::Card* best = nullptr;
			for (auto& c : cards) if (files::inside(path, c.root) && (!best || c.root.size() > best->root.size())) best = &c;
			return best;
		}

		// a folder as the pages say it: the card's name, then the path on it
		std::string where(const std::string& path) const
		{
			const files::Card* c = cardOf(path);
			if (!c) return path;
			return path == c->root ? c->label : c->label + path.substr(c->root.size());
		}

		std::string kept(const std::string& name) const { return files::keptWhy(pathJoin(dir, name), cards, systems); }

		// any of these a game: a file in a card's roms (its play time and favourite go by its place there). Worked out when
		// the actions open or the clipboard is filled, from the listing: not an lstat of each at every frame
		bool gamesAmong(const std::vector<std::string>& ps, const std::vector<bool>& folders) const
		{
			for (size_t i = 0; i < ps.size(); i++)
			{
				const files::Card* c = cardOf(ps[i]);
				if (c && !folders[i] && ps[i].compare(c->root.size(), 6, "/roms/") == 0) return true;
			}
			return false;
		}

		// the second card's roms folder there now and not offered to the library (the sd2 script looks for it only when
		// the card goes in: a fresh card's games, pasted in roms/ just now, stayed out of the library): offered
		void offerRoms(const std::string& path)
		{
			const files::Card* c = cardOf(path);
			struct stat st;
			if (!c || c->id != "sd2" || getenv("BANANA_SD2") || access("/run/banana/sd2/roms", F_OK) == 0) return;
			if (stat((c->root + "/roms").c_str(), &st) == 0 && S_ISDIR(st.st_mode)) ui->sd2Scan();
		}

		// the list: the cards and the servers (a server's pages, a download's, are settings_servers.cpp's), the cursor on
		// the card of the folder last shown
		void home()
		{
			cards = files::cards(ui->romRoot);
			sub = CARDS;
			for (size_t i = 0; i < cards.size(); i++) if (!dir.empty() && files::inside(dir, cards[i].root)) focus(R_CARD, (int)i);
		}

		// the list's cursor on a row of that kind (and index)
		void focus(int kind, int index)
		{
			std::vector<Row> r = rows();
			for (size_t i = 0; i < r.size(); i++)
				if (r[i].kind == kind && (index < 0 || r[i].index == index))
				{
					sel[CARDS] = (int)i;
					if (sel[CARDS] < top[CARDS]) top[CARDS] = sel[CARDS];
					if (sel[CARDS] >= top[CARDS] + ROWS) top[CARDS] = sel[CARDS] - ROWS + 1;
					return;
				}
		}

		// Banana's own folder on the data card, banana/ (the settings, the servers, the jobs): no download or unpacking goes
		// there
		bool inBanana(const std::string& path) const { return files::inside(path, ui->data); }

		std::string clipWhat() const
		{
			if (!rclip.items.empty())
				return rclip.items.size() == 1 ? rclip.items[0].name : plural((long long)rclip.items.size(), "item", "items");
			return what(baseNames(clip));
		}
		bool clipEmpty() const { return clip.empty() && rclip.items.empty(); }

		// a card came or went: the list of cards again, and out of a folder on one that went (a job at work says itself
		// what became of it: its files fail)
		bool cardsChanged()
		{
			std::vector<files::Card> now = files::cards(ui->romRoot);
			bool same = now.size() == cards.size();
			for (size_t i = 0; same && i < now.size(); i++) same = now[i].root == cards[i].root;
			if (same) return false;
			cards = now;
			if (pending) return true;
			if (!dir.empty() && !cardOf(dir))
			{
				message = "The second SD card was taken out.";
				log(message);
				dir.clear();
				entries.clear();
				marked.clear();
				home();
				return true;
			}
			if (sub == CARDS || (cards.size() > 1 && dir.empty())) home();
			return true;
		}

		// back from the keyboard: what cardsChanged() would have done while it was up (the cards as they are now, out of
		// a folder on one that went). The list is compared with none, as entered() may have read it already
		void cardsBack()
		{
			if (!cardsLater) return;
			cardsLater = false;
			cards.clear();
			cardsChanged();
		}

		void open()
		{
			armed = -1;
			pick = NO_PICK;
			systems = files::systemFolders(ui->systemsConf);
			cards = files::cards(ui->romRoot);
			ui->serversRefresh();
			servers = ui->serverList();
			if (pending) { sub = jobShown ? JOB : FOLDER; return; }
			message.clear();
			if (dir.empty() || !cardOf(dir)) { home(); return; }	// (the first time, or the second card taken out)
			openDir(dir);
		}

		// a folder listed (again): the cursor where it was in it, or on focus; marks kept only in the same folder
		void openDir(const std::string& d, const std::string& focus = "")
		{
			if (!dir.empty()) cursor[dir] = { sel[FOLDER], top[FOLDER] };
			std::vector<files::Entry> e;
			std::string why;
			if (!files::list(d, e, why, !job.running()))
			{
				message = where(d) + " can't be read: " + why + ".";
				log(message);
				// a folder opened from the one shown (one of its own): that one stays (not the card's root)
				if (!dir.empty() && d != dir && files::inside(d, dir) && sub == FOLDER) return;
				const files::Card* c = cardOf(d);
				if (c && d != c->root) { openDir(c->root); return; }
				dir.clear();
				entries.clear();
				sub = cards.size() > 1 ? CARDS : FOLDER;
				return;
			}
			bool again = d == dir;
			dir = d;
			entries.swap(e);
			// (a download's .part files here: shown as downloading, left alone)
			parts.clear();
			for (auto& p : ui->fetchParts()) if (files::parent(p) == d) parts.insert(files::baseName(p));
			counts.clear();
			if (!again) marked.clear();
			else
			{
				std::set<std::string> still;
				for (auto& x : entries) if (marked.count(x.name)) still.insert(x.name);
				marked.swap(still);
			}
			sub = FOLDER;
			auto c = cursor.find(d);
			int& s = sel[FOLDER];
			int& t = top[FOLDER];
			s = c == cursor.end() ? 0 : c->second.first;
			t = c == cursor.end() ? 0 : c->second.second;
			if (!focus.empty())
				for (size_t i = 0; i < entries.size(); i++) if (entries[i].name == focus) s = (int)i;
			s = std::max(0, std::min(s, (int)entries.size() - 1));
			if (s < t) t = s;
			if (s >= t + ROWS) t = s - ROWS + 1;
		}

		const files::Entry* current() const
		{
			int s = sel[FOLDER];
			return sub == FOLDER && s >= 0 && s < (int)entries.size() ? &entries[s] : nullptr;
		}

		std::string what(const std::vector<std::string>& names) const { return names.size() == 1 ? names[0] : plural((long long)names.size(), "item", "items"); }

		// ---- the rows -------------------------------------------------------------------------------------------------
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
			case CARDS:
			{
				// (a folder chosen for a download: the cards alone)
				std::string label, value, title, state;
				bool warn = false;
				if (!pick && ui->fetchJobRow(label, value, title, state, warn)) add(label, value, R_JOB)->warn = warn;
				for (size_t i = 0; i < cards.size(); i++)
				{
					files::Space sp;
					add(cards[i].label, files::space(cards[i].root, sp) ? files::bytes(sp.free) + " free" : "", R_CARD, (int)i)->glyph = 2;
				}
				if (pick) break;
				for (size_t i = 0; i < servers.size(); i++) add(servers[i].title, servers[i].protocol, R_SERVER, (int)i)->glyph = 3;
				add("Add a server", "", R_ADD);
				add("Download options", "", R_OPTIONS);
				break;
			}
			case FOLDER:
				// (the words only for the rows on screen and the one chosen: a folder may hold thousands of names, and its
				// rows are made at every press and every frame)
				r.reserve(entries.size());
				for (size_t i = 0; i < entries.size(); i++)
				{
					const files::Entry& e = entries[i];
					int at = (int)i;
					bool shown = at == sel[FOLDER] || (at >= top[FOLDER] && at < top[FOLDER] + ROWS);
					Row* x = add(shown ? e.name : std::string(), !shown ? "" : e.dir ? "Folder" : parts.count(e.name) ? "Downloading" :
						e.known ? files::bytes((uint64_t)e.size) : "", R_ENTRY, at);
					x->mark = shown && marked.count(e.name) > 0;
					x->glyph = e.dir ? 0 : 1;
				}
				break;
			case ACTIONS:
			{
				if (!rclip.items.empty()) add("Paste here", "Download " + clipWhat(), A_PASTE);
				else if (!clip.empty()) add("Paste here", (clipOp == files::Op::COPY ? "Copy " : "Move ") + what(baseNames(clip)), A_PASTE);
				if (!targets.empty())
				{
					// (Move and Delete for what can go: the card's own folders among the marked stay, and say so above)
					add("Copy", what(targets), A_COPY);
					if (!movable.empty()) add("Move", what(movable), A_MOVE);
					if (movable.size() == 1 && targets.size() == 1) add("Rename", "", A_RENAME);
					if (!movable.empty())
					{
						Row* d = add("Delete", what(movable), A_DELETE);
						if (armed == A_DELETE) { d->value = "Press A again"; d->warn = true; }
					}
				}
				if (!unpackables.empty())
					add("Unpack...", unpackables.size() == 1 ? files::baseName(unpackables[0]) : plural((long long)unpackables.size(), "archive", "archives"), A_UNPACK);
				add("New folder", "", A_FOLDER);
				if (!entries.empty()) add(marked.size() < entries.size() ? "Mark all" : "Unmark all", plural((long long)entries.size(), "item", "items"),
					marked.size() < entries.size() ? A_MARKALL : A_UNMARKALL);
				if (!clipEmpty()) add("Clear the clipboard", "", A_CLEAR);
				break;
			}
			case CLASH:
			{
				add("Keep both", "New names", C_KEEP);
				Row* x = add("Replace", "", C_REPLACE);
				if (armed == C_REPLACE) { x->value = "Press A again"; x->warn = true; }
				add("Skip those", "", C_SKIP);
				break;
			}
			case JOB:
				for (size_t i = 0; i < jobSources.size(); i++) add(files::baseName(jobSources[i]), "", J_ITEM, (int)i)->glyph = jobFolders[i] ? 0 : 1;
				break;
			default: break;
			}
			return r;
		}

		static std::vector<std::string> baseNames(const std::vector<std::string>& paths)
		{
			std::vector<std::string> r;
			for (auto& p : paths) r.push_back(files::baseName(p));
			return r;
		}

		// ---- keys -----------------------------------------------------------------------------------------------------
		Result press(Key k)
		{
			if (pending)
			{
				// the job's page: Y stops it (twice); nothing else while it works
				if (k != Key::Y || !jobShown) return NONE;
				if (armed != J_ITEM) { armed = J_ITEM; armedAt = now; message = "Press Y again to stop. What is done stays; the file in hand is left out."; return MOVED; }
				armed = -1;
				job.stop();
				return MOVED;
			}
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
				armed = -1;
				return MOVED;
			};
			switch (k)
			{
			case Key::UP: return moveTo(s - 1);
			case Key::DOWN: return moveTo(s + 1);
			case Key::L: if (!n) return NONE; s = std::max(0, s - ROWS); t = std::max(0, t - ROWS); if (s < t) t = s; armed = -1; return MOVED;
			case Key::R: if (!n) return NONE; s = std::min(n - 1, s + ROWS); if (s >= t + ROWS) t = s - ROWS + 1; armed = -1; return MOVED;
			case Key::START: ui->close(); return CLOSED;
			case Key::B: case Key::LEFT: return back();
			case Key::A:
				// (choosing a folder: A opens one, a file is nothing to choose)
				if (pick && sub == FOLDER) return cur && entries[cur->index].dir ? activate(*cur) : NONE;
				return cur ? activate(*cur) : (sub == FOLDER ? openActions() : NONE);
			// RIGHT only goes in (a card, a folder; not a server, which it would log in to again and again): it repeats
			// while held, and as A it chose Delete's or Replace's second press, or pasted, without a second press of anything
			case Key::RIGHT: return cur && ((sub == CARDS && cur->kind == R_CARD) || (sub == FOLDER && entries[cur->index].dir)) ? activate(*cur) : NONE;
			case Key::X:
				if (sub == FOLDER && pick) return newFolder();
				return sub == FOLDER ? openActions() : NONE;
			case Key::Y:
				if (sub == CARDS && cur && cur->kind == R_SERVER) { ui->editServer(servers[cur->index].id); return MOVED; }
				if (pick) return sub == FOLDER ? choose() : NONE;
				if (sub != FOLDER || !cur) return NONE;
				if (!marked.erase(entries[cur->index].name)) marked.insert(entries[cur->index].name);
				message.clear();
				return MOVED;
			case Key::SELECT:
				if (sub == FOLDER && !dir.empty()) { message.clear(); openDir(dir); return MOVED; }
				if (sub == CARDS) { ui->serversRefresh(); servers = ui->serverList(); home(); return MOVED; }
				return NONE;
			default: return NONE;
			}
		}

		Result back()
		{
			armed = -1;
			switch (sub)
			{
			case CARDS:
				if (pick) { pick = NO_PICK; message.clear(); log("no folder chosen"); ui->pickCancelled(); return MOVED; }	// (the download's folder as it was)
				ui->close();
				return CLOSED;
			case FOLDER:
			{
				message.clear();
				const files::Card* c = cardOf(dir);
				if (dir.empty() || !c || dir == c->root)
				{
					if (!dir.empty()) cursor[dir] = { sel[FOLDER], top[FOLDER] };
					home();
					return MOVED;
				}
				openDir(files::parent(dir), files::baseName(dir));
				return MOVED;
			}
			case CLASH: message = "Nothing was pasted."; clashNames.clear(); sub = FOLDER; return MOVED;
			default: sub = FOLDER; return MOVED;
			}
		}

		Result openActions()
		{
			targets.clear();
			movable.clear();
			targetDirs.clear();
			movableDirs.clear();
			// (a download's .part, still written: left alone)
			for (auto& e : entries) if (marked.count(e.name) && !parts.count(e.name)) { targets.push_back(e.name); targetDirs.push_back(e.dir); }
			if (targets.empty() && marked.empty() && current() && !parts.count(current()->name)) { targets.push_back(current()->name); targetDirs.push_back(current()->dir); }
			unpackables.clear();
			for (size_t i = 0; i < targets.size(); i++) if (!targetDirs[i] && fetch::isArchive(targets[i])) unpackables.push_back(pathJoin(dir, targets[i]));
			for (size_t i = 0; i < targets.size(); i++)
				if (kept(targets[i]).empty()) { movable.push_back(targets[i]); movableDirs.push_back(targetDirs[i]); }
			movableGames = gamesAmong(paths(movable), movableDirs);
			if (dir.empty()) return NONE;
			sub = ACTIONS;
			sel[ACTIONS] = top[ACTIONS] = 0;
			armed = -1;
			message.clear();
			if (sync)
			{
				// (the headless test: which actions were offered, e.g. none that would move a card's own folder)
				std::string l;
				for (auto& r : rows()) l += (l.empty() ? "" : ", ") + r.label;
				log("actions for " + (targets.empty() ? where(dir) : what(targets)) + ": " + l);
			}
			return MOVED;
		}

		std::vector<std::string> paths(const std::vector<std::string>& names) const
		{
			std::vector<std::string> r;
			for (auto& n : names) r.push_back(pathJoin(dir, n));
			return r;
		}

		Result activate(const Row& row)
		{
			switch (row.kind)
			{
			case R_CARD: openDir(cards[row.index].root); return MOVED;
			// (the servers' pages: settings_servers.cpp's)
			case R_JOB: message.clear(); ui->openFetchJob(); return MOVED;
			case R_SERVER: message.clear(); ui->openServer(servers[row.index].id); return MOVED;
			case R_ADD: message.clear(); ui->editServer(""); return MOVED;
			case R_OPTIONS: message.clear(); ui->openServerOptions(); return MOVED;
			case A_UNPACK:
				marked.clear();
				sub = FOLDER;
				ui->unpackArchives(unpackables, dir);
				return MOVED;
			case R_ENTRY:
			{
				const files::Entry& e = entries[row.index];
				if (e.dir) { message.clear(); openDir(pathJoin(dir, e.name)); return MOVED; }
				return openActions();	// (a file: what can be done with it)
			}
			case A_COPY: case A_MOVE:
			{
				const std::vector<std::string>& which = row.kind == A_COPY ? targets : movable;
				clip = paths(which);
				clipDirs = row.kind == A_COPY ? targetDirs : movableDirs;
				clipGames = gamesAmong(clip, clipDirs);
				clipOp = row.kind == A_COPY ? files::Op::COPY : files::Op::MOVE;
				clipFrom = dir;
				rclip = Settings::RemoteClip();	// (one clipboard: a server's files copied before are let go of)
				marked.clear();
				sub = FOLDER;
				message = what(which) + (row.kind == A_COPY ? " to copy" : " to move") + ": open the folder they go to, then X, Paste here.";
				return ACTED;
			}
			case A_PASTE: return paste();
			case A_RENAME:
			{
				renaming = targets[0];
				ui->startEntry(ENTRY_RENAME, "Rename", renaming, FILES, renaming);
				std::string help = "A types the key, B deletes, X capitals, L symbols, Y a space, START renames it, SELECT leaves it as it was. Or tap the keys. "
					"Not in a name: \\ / : * ? \" < > |.";
				const files::Card* c = cardOf(dir);
				std::string rel = c ? dir.substr(std::min(dir.size(), c->root.size())) : "";
				bool file = false;
				for (auto& e : entries) if (e.name == renaming) file = !e.dir;
				if (file && (rel == "/roms" || rel.compare(0, 6, "/roms/") == 0))
					help += " A game's saves, play time and star in Favorites go by its file name: renamed, it starts without them (they stay where they are).";
				ui->entryHelp = help;
				return MOVED;
			}
			case A_DELETE:
				if (armed != A_DELETE) { armed = A_DELETE; return MOVED; }
				armed = -1;
				start(files::Op::DELETE, paths(movable), movableDirs, "", files::Clash::SKIP);
				return ACTED;
			case A_FOLDER: return newFolder();
			case A_MARKALL: for (auto& e : entries) marked.insert(e.name); sub = FOLDER; return MOVED;
			case A_UNMARKALL: marked.clear(); sub = FOLDER; return MOVED;
			case A_CLEAR: clip.clear(); rclip = Settings::RemoteClip(); message = "The clipboard is empty."; sub = FOLDER; return MOVED;
			case C_KEEP: case C_SKIP: case C_REPLACE:
				if (row.kind == C_REPLACE && armed != C_REPLACE) { armed = C_REPLACE; return MOVED; }
				armed = -1;
				start(clipOp, clip, clipDirs, dir, row.kind == C_KEEP ? files::Clash::KEEP_BOTH : row.kind == C_REPLACE ? files::Clash::REPLACE : files::Clash::SKIP);
				return ACTED;
			default: return NONE;
			}
		}

		// the card of a folder still in, by the mount table read now (not as the pages last saw it: taken out while the
		// keyboard was up, or before its event came, the folder is its mount point's own in /run, the RAM); else back to
		// the cards, and why
		bool cardStill(const std::string& path)
		{
			cards = files::cards(ui->romRoot);
			if (cardOf(path)) return true;
			message = "The second SD card was taken out.";
			log(message);
			dir.clear();
			entries.clear();
			marked.clear();
			home();
			return false;
		}

		Result newFolder()
		{
			ui->startEntry(ENTRY_FOLDER, "New folder in", where(dir), FILES, "");
			ui->entryHelp = "A types the key, B deletes, X capitals, L symbols, Y a space, START makes the folder, SELECT cancels. Or tap the keys. "
				"Not in a name: \\ / : * ? \" < > |.";
			return MOVED;
		}

		// the folder shown chosen for a download or an unpacking: back to its page
		Result choose()
		{
			if (dir.empty() || !cardStill(dir)) return MOVED;
			if (inBanana(dir)) { message = "Not into banana/: it holds Banana's own files (the settings, the servers, the jobs). Choose another folder."; return MOVED; }
			pick = NO_PICK;
			message.clear();
			log("folder chosen: " + dir);
			ui->folderPicked(dir);
			return ACTED;
		}

		Result paste()
		{
			if (!cardStill(dir)) return MOVED;
			if (!rclip.items.empty())
			{
				// a server's files: the Download page, on this folder
				sub = FOLDER;
				if (inBanana(dir)) { message = "Not into banana/: it holds Banana's own files (the settings, the servers, the jobs)."; log("paste refused: " + message); return MOVED; }
				std::string why = ui->pasteDownload(rclip, dir);
				if (!why.empty()) { message = why; rclip = Settings::RemoteClip(); log("paste refused: " + why); return MOVED; }
				log("download " + std::to_string(rclip.items.size()) + " from " + rclip.title + " into " + dir);
				return MOVED;
			}
			std::string why = files::pasteProblem(clip, dir);
			if (!why.empty()) { message = why; log("paste refused: " + why); sub = FOLDER; return MOVED; }
			clashNames = files::clashes(clip, dir);
			if (clashNames.empty()) { start(clipOp, clip, clipDirs, dir, files::Clash::KEEP_BOTH); return ACTED; }
			sub = CLASH;
			sel[CLASH] = top[CLASH] = 0;
			armed = -1;
			return MOVED;
		}

		// folders: which sources are folders, as the listing said (a stat of each here held the frames up: a Mark all of
		// thousands)
		void start(files::Op op, const std::vector<std::string>& sources, const std::vector<bool>& folders, const std::string& dest, files::Clash clash)
		{
			jobOp = op;
			jobSources = sources;
			jobFolders = folders;
			jobDir = op == files::Op::DELETE ? dir : dest;
			jobWhere = where(jobDir);
			const files::Card* card = cardOf(dest);
			pending = true;
			jobShown = false;
			// its page 300 ms from the first update after this (a job over sooner never shows it), not from now: now is where
			// the host's loop last looked, and it may have slept in poll() a minute since
			jobDue = -1;
			sel[JOB] = top[JOB] = 0;
			message.clear();
			sub = FOLDER;
			log(std::string(op == files::Op::COPY ? "copy" : op == files::Op::MOVE ? "move" : "delete") + " " + std::to_string(sources.size()) +
				(op == files::Op::DELETE ? " in " + dir : " into " + dest));
			// (dest's card as it was found mounted: pasted on that one, or not at all)
			job.start(op, sources, dest, clash, sync, ui->wakeHost, card ? card->dev : 0);
			if (sync) finish();
		}

		// the job told: back to its folder, the cursor on what it put there, its outcome on the top panel
		void finish()
		{
			pending = jobShown = false;
			armed = -1;
			files::Progress pr = job.progress();
			int n = (int)jobSources.size();
			bool del = jobOp == files::Op::DELETE;
			std::string verb = del ? "Deleted" : jobOp == files::Op::COPY ? "Copied" : "Moved";
			std::string m;
			if (!pr.error.empty() && pr.done == 0 && pr.failed >= n && !pr.stopped) m = pr.error;	// (refused before a byte)
			else
			{
				// what was asked for, as it was asked: files, folders, or items when both
				int folders = (int)std::count(jobFolders.begin(), jobFolders.end(), true);
				const char* one = folders == 0 ? "file" : folders == n ? "folder" : "item";
				const char* many = folders == 0 ? "files" : folders == n ? "folders" : "items";
				std::string into = del ? "." : " into " + jobWhere + ".";
				if (pr.stopped) m = "Stopped: " + std::to_string(pr.done) + " of " + std::to_string(std::max(pr.items, pr.done)) + (del ? " deleted." : " done.");
				else if (pr.sourcesDone == n)
				{
					// (an empty folder copied is a folder, not "0 files"; a folder merged into another is one, not what was
					// renamed inside it)
					m = verb + " " + plural(n, one, many);
					if (!del && pr.bytes > 0) m += " (" + files::bytes(pr.bytes) + ")";
					m += into;
				}
				else if (pr.done > 0)
				{
					// (some of it: files when they were copied one by one; a delete's entries, a move's renames on one card: items)
					bool byFile = !del && (jobOp == files::Op::COPY || pr.total > 0);
					m = verb + " " + std::to_string(pr.done) + " of " + plural(std::max(pr.items, pr.done), byFile ? "file" : "item", byFile ? "files" : "items") + into;
				}
				else if (pr.sourcesDone > 0) m = verb + " " + std::to_string(pr.sourcesDone) + " of " + plural(n, one, many) + into;	// (empty folders)
				else m = del ? "Nothing was deleted." : "Nothing was pasted.";
				if (pr.skipped) m += " " + std::to_string(pr.skipped) + (pr.skipped == 1 ? " left as it was." : " left as they were.");
				if (pr.failed) m += " " + std::to_string(pr.failed) + " couldn't be: " + pr.error;
			}
			// (the card's own word once all of it was written: no one file's)
			if (pr.unwritten) m += " But a card refused a write at the end, or was taken out: some of it may not be on it.";
			if (jobOp == files::Op::MOVE) clip.clear();	// (moved: they aren't where the clipboard says any more)
			if (del) marked.clear();
			clashNames.clear();
			// (its card taken out meanwhile: the cards, not the mount point's own folder in /run)
			cards = files::cards(ui->romRoot);
			if (cardOf(jobDir)) openDir(jobDir, pr.placed.empty() ? "" : pr.placed[0]);
			else { dir.clear(); entries.clear(); home(); }
			marked.clear();
			message = m;
			log(m);
			offerRoms(jobDir);
		}

		// ---- the keyboard's text --------------------------------------------------------------------------------------
		bool entered(Entry e, const std::string& text, std::string& why)
		{
			// (the card taken out while the keyboard was up: nothing made in its mount point's own folder. dir stays as it
			// is: the keyboard is still up, and a name typed again goes nowhere else; out of it once it is down, cardsBack)
			cards = files::cards(ui->romRoot);
			if (!cardOf(dir)) { why = "The second SD card was taken out."; log(why); return false; }
			if (e == ENTRY_FOLDER)
			{
				if (!files::makeFolder(dir, text, why)) { log("new folder refused: " + why); return false; }
				// (choosing a folder: the one made is where it goes, so it is opened)
				if (pick) openDir(pathJoin(dir, text));
				else openDir(dir, text);
				message = "Made the folder " + text + ".";
				log(message);
				offerRoms(dir);
				return true;
			}
			if (!files::renameEntry(dir, renaming, text, why)) { log("rename refused: " + why); return false; }
			openDir(dir, text);
			message = renaming == text ? "" : "Renamed " + renaming + " to " + text + ".";
			if (!message.empty()) log(message);
			offerRoms(dir);
			return true;
		}

		// ---- touch ------------------------------------------------------------------------------------------------------
		Result touch(float cx, float cy)
		{
			(void)cx;
			float dx = ui->downX, dy = ui->downY;
			std::vector<Row> r = rows();
			if (dy >= 418 && dy <= 466)
			{
				Key k = ui->chipAt(chips(r), dx);
				return k == Key::NONE ? NONE : press(k);
			}
			if (pending) return NONE;
			int& s = sel[sub];
			int& t = top[sub];
			// a swipe up or down scrolls a page, wherever it started (first: one that starts on a row isn't a tap on it)
			if (std::fabs(cy - dy) > 40)
			{
				if (r.empty()) return NONE;
				int n = (int)r.size();
				t = std::max(0, std::min(std::max(0, n - ROWS), t + (cy < dy ? ROWS : -ROWS)));
				s = std::max(t, std::min(s, t + ROWS - 1));
				armed = -1;
				return MOVED;
			}
			for (int i = 0; i < ROWS && t + i < (int)r.size(); i++)
			{
				float ry = 82 + i * 64;
				if (dy < ry || dy > ry + 56) continue;
				int at = t + i;
				// a folder's row: the tick's place marks and unmarks
				if (sub == FOLDER && dx < 70) { s = at; return press(Key::Y); }
				if (at != s) { s = at; armed = -1; return MOVED; }
				return press(Key::A);
			}
			return NONE;
		}

		// ---- background -------------------------------------------------------------------------------------------------
		bool update(double t)
		{
			now = t;
			if (!pending) return false;
			if (!job.running()) { finish(); return true; }
			if (jobDue < 0) jobDue = t + 300;
			// Stop's first press lets go after 3 s (a second press much later isn't the one it asked for)
			if (armed == J_ITEM && t - armedAt > 3000) { armed = -1; message.clear(); return true; }
			if (!jobShown && t >= jobDue) { jobShown = true; sub = JOB; lastDraw = t; return true; }
			if (jobShown && t - lastDraw >= 500) { lastDraw = t; return true; }	// (its progress, twice a second)
			return false;
		}

		int wake(double t) const
		{
			if (!pending) return -1;
			if (!job.running() || jobDue < 0) return 0;
			return std::max(0, (int)((jobShown ? lastDraw + 500 : jobDue) - t));
		}

		// ---- drawing ----------------------------------------------------------------------------------------------------
		struct Top { std::string label, title, accent, help; bool meter = false; double fraction = 0; std::string meterLine; };

		Top topPanel(const std::vector<Row>& r)
		{
			Top t;
			const Row* cur = sel[sub] < (int)r.size() ? &r[sel[sub]] : nullptr;
			std::string clipNote;
			if (!rclip.items.empty())
				clipNote = " In the clipboard: " + clipWhat() + " to download, from " + rclip.title + " " + rclip.dir + ".";
			else if (!clip.empty())
				clipNote = " In the clipboard: " + what(baseNames(clip)) + (clipOp == files::Op::COPY ? " to copy" : " to move") + ", from " + where(clipFrom) + ".";
			// (choosing a folder: what it is for, not the clipboard)
			if (pick) clipNote.clear();
			std::string pickNote = pick ? (pick == PICK_UNPACK ? "Unpacking " : "Downloading ") + pickWhat + ": " : "";
			switch (sub)
			{
			case CARDS:
			{
				t.label = pick ? "Choose a folder" : "Files";
				t.title = cur ? cur->label : "Files";
				int kind = cur ? cur->kind : -1;
				if (kind == R_CARD)
				{
					files::Space sp;
					if (files::space(cards[cur->index].root, sp)) t.accent = files::bytes(sp.free) + " free of " + files::bytes(sp.total);
					t.help = pick ? pickNote + "A opens a card, then its folders: Y takes the folder shown, X makes a new one. B goes back to the download as it was."
						: "The cards' folders and files: A opens a card. In a folder, Y marks, X copies, moves, renames, deletes or unpacks what is marked, or makes a new folder.";
				}
				else if (kind == R_JOB)
				{
					std::string label, value, title, state;
					bool warn = false;
					ui->fetchJobRow(label, value, title, state, warn);
					t.title = title;
					t.accent = state;
					t.help = "A shows each file. It carries on in the background while you play (paused) or the menu shows anything else; with the lid closed it finishes before the handheld sleeps.";
				}
				else if (kind == R_SERVER)
				{
					const Settings::ServerInfo& v = servers[cur->index];
					t.accent = v.address;
					t.help = "A opens its folders: Y marks, X downloads what is marked into a folder on a card, or copies it for Paste here in one. Y here changes the server or deletes it.";
					if (v.plainFtp) t.help += " Plain FTP sends the password unencrypted.";
				}
				else if (kind == R_ADD) t.help = "A NAS, or a computer sharing a folder: over SMB (Windows' and macOS's file sharing, most NAS), SFTP (SSH) or FTP. Its files are downloaded onto the cards, archives unpacked.";
				else if (kind == R_OPTIONS) t.help = "Unpacking after a download, the archive deleted after, an archive's single folder unwrapped, the servers' hidden files.";
				t.help += clipNote;
				break;
			}
			case FOLDER:
			{
				t.label = where(dir);
				const files::Entry* e = current();
				if (!e)
				{
					t.title = dir.empty() ? "Files" : files::baseName(dir);
					t.accent = dir.empty() ? "" : "An empty folder";
					t.help = dir.empty() ? "" : pick ? pickNote + "Y takes this folder, X makes a new one in it. B goes up."
						: "X makes a new folder here" + std::string(clipEmpty() ? "." : ", or pastes.") + clipNote;
					break;
				}
				t.title = e->name;
				if (e->dir)
				{
					std::string p = pathJoin(dir, e->name);
					auto c = counts.find(p);
					int n = c != counts.end() ? c->second : (counts[p] = files::count(p));
					t.accent = n < 0 ? "Folder" : "Folder \xc2\xb7 " + plural(n, "item", "items");
				}
				else t.accent = files::bytes((uint64_t)e->size) + (e->mtime > 0 ? " \xc2\xb7 " + date(e->mtime) : "");
				std::string name = ui->fit(e->name, true, 30, 420) == e->name ? "" : e->name + ". ";
				t.help = name + (e->dir ? "A opens it, " : "") + "Y marks, X: copy, move, rename, delete, unpack, a new folder. B goes up." + clipNote;
				if (parts.count(e->name)) t.help = name + "A download is writing it: it takes its own name once it is whole." + clipNote;
				if (!marked.empty()) t.help = plural((long long)marked.size(), "item marked", "items marked") + ": X for what to do with them." + clipNote;
				if (pick) t.help = pickNote + name + (e->dir ? "A opens it. " : "") + "Y takes the folder shown (" + where(dir) + "), X makes a new one in it. B goes up; from the cards, back to the download as it was.";
				break;
			}
			case ACTIONS:
			{
				t.label = where(dir);
				t.title = cur ? cur->label : "";
				t.accent = targets.empty() ? "" : what(targets);
				std::string keep;
				std::vector<std::string> stay;	// the card's own folders among the marked
				for (auto& n : targets) { std::string k = kept(n); if (!k.empty()) { stay.push_back(n); if (keep.empty()) keep = k; } }
				// a game moved: what goes by its place (systems' folders, either card)
				const char* gameMoved = " A game's play time and its star in Favorites go by its place: moved, it starts without them (its saves go by its name and stay).";
				switch (cur ? cur->kind : -1)
				{
				case A_PASTE:
					if (!rclip.items.empty())
					{
						t.accent = "Download " + clipWhat();
						t.help = "From " + rclip.title + " into this folder: " + where(dir) + ". The next page says how (archives unpacked, files already here).";
						break;
					}
					t.accent = (clipOp == files::Op::COPY ? "Copy " : "Move ") + what(baseNames(clip));
					t.help = "Into this folder: " + where(dir) + ". A name already here asks first." +
						(clipOp == files::Op::MOVE ? " Moved on one card it is at once; from the other card each file is copied, then deleted where it was." : "");
					if (clipOp == files::Op::MOVE && clipGames) t.help += gameMoved;
					break;
				case A_COPY: t.help = "Into the clipboard: then open the folder it goes to, and X, Paste here."; break;
				case A_MOVE: t.accent = what(movable);
					t.help = "Into the clipboard, to be moved: then open the folder it goes to, and X, Paste here.";
					if (movableGames) t.help += gameMoved;
					break;
				case A_RENAME: t.help = "A new name, typed on the keyboard."; break;
				case A_DELETE: t.accent = what(movable);
					t.help = armed == A_DELETE ? "Press A again to delete " + what(movable) + " for good (a folder with everything in it, hidden files too)." : "For good: there is no bin. A twice."; break;
				case A_FOLDER: t.accent = ""; t.help = "A folder in " + where(dir) + ", its name typed on the keyboard."; break;
				case A_UNPACK: t.accent = cur->value;
					t.help = "Into a folder chosen on the next page (this one first): zip, 7z and RAR archives, multi-volume ones by their first part, one with a password asked for it.";
					break;
				case A_MARKALL: case A_UNMARKALL: t.accent = ""; t.help = "Every entry of this folder, for Copy, Move or Delete."; break;
				case A_CLEAR: t.accent = ""; t.help = "What Copy or Move put in it is let go of: nothing on the cards or the servers changes."; break;
				default: break;
				}
				if (cur && cur->kind != A_PASTE && cur->kind != A_FOLDER && cur->kind != A_UNPACK)
				{
					// all of them the card's own: why; some of them: which stay where they are
					if (!keep.empty() && movable.empty()) t.help += " " + keep + " So it can be copied, not moved, renamed or deleted.";
					else if (!stay.empty() && (cur->kind == A_MOVE || cur->kind == A_DELETE))
						t.help += " Not " + what(stay) + ": the card's own " + (stay.size() == 1 ? "folder stays" : "folders stay") + " where " + (stay.size() == 1 ? "it is." : "they are.");
				}
				t.help += clipNote;
				break;
			}
			case CLASH:
			{
				t.label = where(dir);
				t.title = "Already here";
				t.accent = std::to_string(clashNames.size()) + " of " + plural((long long)clip.size(), "item", "items");
				std::string names;
				for (size_t i = 0; i < clashNames.size() && i < 3; i++) names += (i ? ", " : "") + clashNames[i];
				if (clashNames.size() > 3) names += "...";
				t.help = names + ". Keep both gives the new ones another name (2); Replace puts them over the old ones (a folder: the two in one); Skip those leaves them out. B pastes nothing.";
				break;
			}
			case JOB:
			{
				files::Progress pr = job.progress();
				bool del = pr.op == files::Op::DELETE;
				t.label = del ? "Deleting" : pr.op == files::Op::COPY ? "Copying" : "Moving";
				t.title = pr.state == files::Progress::COUNTING ? "Counting..." : pr.current.empty() ? where(jobDir) : pr.current;
				if (pr.state == files::Progress::COUNTING) t.accent = pr.items > 0 ? plural(pr.items, del ? "item" : "file", del ? "items" : "files") + " so far" : "";
				else t.accent = pr.items > 0 ? std::to_string(std::min(pr.items, pr.done + 1)) + " of " + plural(pr.items, del ? "item" : "file", del ? "items" : "files") : "";
				t.help = message.empty() ? (del ? "Files stays open till it is done. Y stops: what is deleted stays deleted." : "Each file goes in under a hidden name and takes its own once it is whole. Files stays open till it is done; Y stops: what is done stays.") : message;
				t.meter = true;
				if (pr.total > 0)
				{
					t.fraction = (double)pr.bytes / pr.total;
					double speed = pr.seconds > 0.5 ? pr.bytes / pr.seconds : 0;
					// (a file that grew while it was copied: more bytes than were counted, none left)
					uint64_t left = pr.bytes < pr.total ? pr.total - pr.bytes : 0;
					t.meterLine = files::bytes(pr.bytes) + " of " + files::bytes(pr.total);
					if (speed > 0) t.meterLine += " \xc2\xb7 " + files::bytes((uint64_t)speed) + "/s \xc2\xb7 " + duration(std::min(left / speed, 359999.0)) + " left";
				}
				else
				{
					t.fraction = pr.items > 0 ? (double)pr.done / pr.items : 0;
					t.meterLine = pr.items > 0 ? std::to_string(pr.done) + " of " + std::to_string(pr.items) : "";
				}
				break;
			}
			default: break;
			}
			if (!message.empty() && sub != JOB) t.help = message + " " + t.help;
			return t;
		}

		// a row's glyph on the look's 2 px grid: a folder, a file, a card
		void glyph(int g, float x, float y, const Color& c)
		{
			auto r = [&](float rx, float ry, float rw, float rh) { ui->be->fill(std::round(ui->X(x + rx)), std::round(ui->X(y + ry)), std::round(ui->X(rw)), std::round(ui->X(rh)), c); };
			if (g == 0) { r(0, 0, 8, 2); r(0, 2, 20, 14); }
			else if (g == 1) { r(0, 0, 10, 18); r(10, 4, 4, 14); r(10, 2, 2, 2); }
			else if (g == 3) { r(0, 0, 18, 7); r(0, 9, 18, 7); r(8, 16, 2, 2); r(2, 18, 14, 2); }	// (a server: two drives on a stand)
			else { r(4, 0, 10, 4); r(0, 4, 14, 14); }
		}

		// a folder's path in a line: its end when it is too long ("...nds/Action"), measured in the style it is drawn in
		// (the top panel's label: capitals, letter-spaced)
		std::string headerPath(const std::string& p, float size, float width, bool upper = false, float ls = 0)
		{
			if (ui->text(p, true, size, 0, 0, Color(), upper, ls, true) <= width) return p;
			std::string t = p;
			while (t.find('/') != std::string::npos)
			{
				t = t.substr(t.find('/') + 1);
				if (ui->text(".../" + t, true, size, 0, 0, Color(), upper, ls, true) <= width) return ".../" + t;
			}
			return ui->fit(p, true, size, width, upper, ls);
		}

		void render()
		{
			SPal p = pal(ui->light);
			// a folder's cursor kept in its window first (the rows' words are made for that window alone), then the size
			// and time of the rows on screen read now
			if (sub == FOLDER)
			{
				int& s = sel[FOLDER];
				int& tp = top[FOLDER];
				if (s >= (int)entries.size()) s = std::max(0, (int)entries.size() - 1);
				if (s < tp) tp = s;
				if (s >= tp + ROWS) tp = s - ROWS + 1;
				for (int i = tp; i < tp + ROWS && i < (int)entries.size(); i++)
					if (!entries[i].known) files::details(dir, entries[i]);
			}
			std::vector<Row> r = rows();
			Top t = topPanel(r);
			rnds::Backend* be = ui->be;
			be->beginScreen(0);
			ui->box("screen", false, 0, 0, 640, 480);
			ui->box("hero", false, 18, 20, 604, 436);
			ui->icon("files", 50, 56, 3);
			ui->text(headerPath(t.label, 17, 420, true, 1.4f), true, 17, 170, 62, p.muted, true, 1.4f);
			ui->text(ui->fit(t.title.empty() ? " " : t.title, true, 30, 420), true, 30, 170, 88, p.ink);
			if (!t.accent.empty()) ui->text(ui->fit(t.accent, true, 22, 420), true, 22, 170, 130, p.accent);
			ui->wrapped(t.help, 20, 50, 196, 540, t.meter ? 4 : 10, p.muted);
			if (t.meter)
			{
				ui->meter(50, 330, 540, t.fraction);
				if (!t.meterLine.empty()) ui->text(ui->fit(t.meterLine, false, 18, 540), false, 18, 50, 354, p.muted);
			}
			be->endScreen();

			be->beginScreen(1);
			ui->box("screen", false, 0, 0, 640, 480);
			ui->box("bubble", false, 18, 14, 604, 54);
			std::string count = r.empty() ? "" : sub == JOB ? plural((long long)r.size(), "item", "items") : std::to_string(sel[sub] + 1) + " / " + std::to_string(r.size());
			if (sub == FOLDER && !marked.empty()) count = std::to_string(marked.size()) + " marked";
			float cw = ui->text(count, true, 17, 0, 0, p.muted, false, 0, true);
			std::string head = sub == CARDS ? (pick ? "Choose a folder" : "Files") : sub == ACTIONS ? "What to do" : sub == CLASH ? "Already here" :
				sub == JOB ? t.label : dir.empty() ? "Files" : headerPath(where(dir), 22, 540 - cw);
			ui->text(ui->fit(head, true, 22, 540 - cw), true, 22, 38, 28, p.ink);
			ui->text(count, true, 17, 602 - cw, 31, p.muted);
			int& s = sel[sub];
			int& tp = top[sub];
			if (s >= (int)r.size()) s = std::max(0, (int)r.size() - 1);
			if (s < tp) tp = s;
			if (s >= tp + ROWS) tp = s - ROWS + 1;
			for (int i = 0; i < ROWS && tp + i < (int)r.size(); i++)
			{
				const Row& row = r[tp + i];
				bool on = tp + i == s && sub != JOB;
				float y = 82 + i * 64;
				ui->box(on ? "pick" : "slot", on, 18, y, 604, 56);
				Color c = on ? p.onPick : p.muted;
				Color vc = row.warn ? (on ? p.onPickAccent : p.accent) : c;
				float lx = 38;
				if (sub == FOLDER) { if (row.mark) ui->tick(38, y + 21, on ? p.onPick : p.accent); lx = 62; }
				if (row.glyph >= 0) { glyph(row.glyph, lx, y + 19, c); lx += 30; }
				std::string v = row.value.empty() ? "" : ui->fit(row.value, true, 19, 300);
				float vw = v.empty() ? 0 : ui->text(v, true, 19, 0, 0, vc, false, 0, true);
				ui->text(ui->fit(row.label, true, 20, 586 - vw - 16 - lx), true, 20, lx, y + 16, c);
				if (!v.empty()) ui->text(v, true, 19, 586 - vw, y + 17, vc);
			}
			if (r.empty() && sub == FOLDER) ui->text(dir.empty() ? "Nothing to show." : "An empty folder.", true, 20, 38, 98, p.muted);
			ui->drawChips(chips(r));
			be->endScreen();
		}

		// the buttons, drawn and tapped from this one list: B on the left (and the page's others after it), A on the right
		std::vector<Settings::Chip> chips(const std::vector<Row>& r)
		{
			if (pending) return jobShown ? std::vector<Settings::Chip>{ { Key::Y, "Y", armed == J_ITEM ? "Stop!" : "Stop" } } : std::vector<Settings::Chip>();
			const Row* cur = sel[sub] < (int)r.size() ? &r[sel[sub]] : nullptr;
			const files::Card* c = cardOf(dir);
			bool atRoot = dir.empty() || !c || dir == c->root;
			std::vector<Settings::Chip> cs = { { Key::B, "B", sub == CARDS ? (pick ? "Cancel" : "Close") :
				sub == FOLDER ? (atRoot ? "Back" : "Up") : sub == CLASH ? "Cancel" : "Back" } };
			std::string a;
			switch (sub)
			{
			case CARDS:
				a = !cur ? "" : cur->kind == R_SERVER ? "Connect" : cur->kind == R_ADD ? "Add" : "Open";
				if (cur && cur->kind == R_SERVER) cs.push_back({ Key::Y, "Y", "Edit" });
				break;
			case FOLDER:
				if (pick)
				{
					// (choosing a folder: Y takes the one shown, X makes one in it)
					if (!dir.empty() && !inBanana(dir)) cs.push_back({ Key::Y, "Y", pick == PICK_UNPACK ? "Unpack here" : "Download here" });
					if (!dir.empty()) cs.push_back({ Key::X, "X", "New folder" });
					a = cur && entries[cur->index].dir ? "Open" : "";
					break;
				}
				if (cur) cs.push_back({ Key::Y, "Y", marked.count(entries[cur->index].name) ? "Unmark" : "Mark" });
				if (!dir.empty()) cs.push_back({ Key::X, "X", "Actions" });
				a = cur && entries[cur->index].dir ? "Open" : "";
				break;
			case ACTIONS:
				a = !cur ? "" : cur->kind == A_DELETE && armed == A_DELETE ? "Confirm" : cur->kind == A_PASTE ? "Paste" : cur->kind == A_UNPACK ? "Unpack" :
					cur->kind == A_RENAME || cur->kind == A_FOLDER ? "Type" : "Choose";
				break;
			case CLASH: a = !cur ? "" : cur->kind == C_REPLACE && armed == C_REPLACE ? "Confirm" : "Choose"; break;
			default: break;
			}
			if (!a.empty()) cs.push_back({ Key::A, "A", a, true });
			return cs;
		}
	};

	// ---- Settings' side -------------------------------------------------------------------------------------------
	void Settings::openFiles()
	{
		if (!fm)
		{
			fm = std::make_shared<Files>();
			fm->ui = this;
		}
		fm->sync = sync && !filesAsync;
		fm->now = now;
		mOpen = true;
		page = FILES;
		fm->open();
	}

	// (the keyboard up for a name: its folder stays as it is till the keyboard is down, then the cards are looked at: out
	// of it there, a name typed for the second card went into This card's root)
	bool Settings::cardsChanged()
	{
		if (serversCardsChanged()) return true;	// (a download's page: its card may have gone)
		if (fm && page == KEYBOARD && entryBack == FILES) { fm->cardsLater = true; return false; }
		return fm && page == FILES && fm->cardsChanged();
	}

	// ---- the servers' pages' ways back into Files (settings_servers.cpp) ---------------------------------------------
	void Settings::filesShow(const std::string& dir, const std::string& focus, bool actions)
	{
		if (!fm) { fm = std::make_shared<Files>(); fm->ui = this; }
		page = FILES;
		fm->pick = Files::NO_PICK;
		fm->armed = -1;
		fm->cards = files::cards(romRoot);
		if (dir.empty() || !fm->cardOf(dir)) { fm->home(); return; }
		fm->openDir(dir, focus);
		if (actions) fm->openActions();
	}

	void Settings::filesRoot(const std::string& focus, const std::string& msg)
	{
		if (!fm) { fm = std::make_shared<Files>(); fm->ui = this; }
		page = FILES;
		fm->pick = Files::NO_PICK;
		fm->armed = -1;
		fm->servers = serverList();
		fm->home();
		fm->message = msg;
		if (focus == "job") fm->focus(Files::R_JOB, -1);
		else if (focus == "add") fm->focus(Files::R_ADD, -1);
		else if (focus == "options") fm->focus(Files::R_OPTIONS, -1);
		else if (focus.compare(0, 7, "server:") == 0)
			for (size_t i = 0; i < fm->servers.size(); i++) if (fm->servers[i].id == focus.substr(7)) fm->focus(Files::R_SERVER, (int)i);
	}

	void Settings::pickFolder(int purpose, const std::string& start, const std::string& what)
	{
		if (!fm) { fm = std::make_shared<Files>(); fm->ui = this; }
		page = FILES;
		fm->pick = purpose == 2 ? Files::PICK_UNPACK : Files::PICK_DOWNLOAD;
		fm->pickWhat = what;
		fm->armed = -1;
		fm->message.clear();
		fm->cards = files::cards(romRoot);
		if (start.empty() || !fm->cardOf(start)) fm->home();
		else fm->openDir(start);
	}

	void Settings::setRemoteClip(const RemoteClip& c)
	{
		if (!fm) { fm = std::make_shared<Files>(); fm->ui = this; }
		fm->clip.clear();
		fm->rclip = c;
	}
	bool Settings::filesWorking() const { return fm && fm->job.running(); }

	void Settings::offerSd2Roms()
	{
		for (auto& c : files::cards(romRoot))
		{
			struct stat st;
			if (c.id != "sd2" || getenv("BANANA_SD2") || access("/run/banana/sd2/roms", F_OK) == 0) continue;
			if (stat((c.root + "/roms").c_str(), &st) == 0 && S_ISDIR(st.st_mode)) sd2Scan();
		}
	}
	Settings::Result Settings::pressFiles(Key k) { if (!fm) return NONE; fm->cardsBack(); return fm->press(k); }
	Settings::Result Settings::touchFiles(float cx, float cy) { if (!fm) return NONE; fm->cardsBack(); return fm->touch(cx, cy); }
	void Settings::renderFiles() { if (fm) { fm->cardsBack(); fm->render(); } }
	bool Settings::updateFiles(double t) { return fm && fm->update(t); }
	int Settings::filesWake(double t) const { return fm ? fm->wake(t) : -1; }

	// the keyboard's name: the folder made, or the entry renamed; a name refused keeps the keyboard, the reason above it
	Settings::Result Settings::finishFilesEntry()
	{
		std::string why;
		if (!fm) { page = FILES; return MOVED; }
		if (!fm->entered(entry, pw, why)) { message = why; return MOVED; }
		pw.clear();
		message.clear();
		page = FILES;
		return ACTED;
	}
}
