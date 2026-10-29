// banana-ui: Bshare, the shelf's tile. Files between this handheld and phones or computers with LocalSend on the same
// Wi-Fi (as the settings' pages: a list on the bottom panel, what is chosen explained on the top one). The user's plan:
//   home      Receive, Send, Options
//   receive   seen as the DS's name while open; a device asking: Accept, Decline; Location (a folder of the cards'
//             for the files instead of the usual places: games into their system's folder, the rest into
//             banana/received)
//   send      Device (one seen, LocalSend open), Files (the cards: A opens a folder or picks a file, Y picks a folder),
//             Send (START too): then its progress, Stop
//   options   Receive when closed (Off, Ask, Quick save), About Bshare
// banana-bshare does the network (bshare.cpp): its "serve" runs while the app is open or Options lets it receive, its
// "send" for each send. They talk through /run/banana's bshare-* files; this reads and draws them.
#include "settings.h"
#include "settings_defaults.h"
#include "settings_draw.h"
#include <algorithm>
#include <random>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using rnds::Color;

namespace banana
{
	namespace
	{
		// a path with its "." and ".." worked out and no doubled or trailing slash
		std::string cleanPath(const std::string& p)
		{
			std::vector<std::string> parts;
			std::string cur;
			for (size_t i = 0; i <= p.size(); i++)
			{
				char c = i < p.size() ? p[i] : '/';
				if (c == '\\') c = '/';
				if (c != '/') { cur += c; continue; }
				if (cur == "..") { if (!parts.empty()) parts.pop_back(); }
				else if (!cur.empty() && cur != ".") parts.push_back(cur);
				cur.clear();
			}
			std::string r;
			for (auto& x : parts) r += "/" + x;
			return r.empty() ? "/" : r;
		}
		std::string parentPath(const std::string& p) { return cleanPath(cleanPath(p) + "/.."); }
		std::string baseName(const std::string& p)
		{
			std::string c = cleanPath(p);
			return c == "/" ? "" : c.substr(c.rfind('/') + 1);
		}

		// a size as people read it: "820 B", "4.2 MB", "31 GB"
		std::string bytes(long long n)
		{
			static const char* const UNIT[] = { "B", "KB", "MB", "GB", "TB" };
			double v = (double)(n < 0 ? 0 : n);
			int u = 0;
			while (v >= 1024 && u < 4) { v /= 1024; u++; }
			char b[32];
			if (u == 0) snprintf(b, sizeof b, "%lld B", n < 0 ? 0 : n);
			else snprintf(b, sizeof b, v < 10 ? "%.1f %s" : "%.0f %s", v, UNIT[u]);
			return b;
		}

		// the cards a transfer can go to: this one, and the RG DS Plus's second card while it is in (bananad's sd2
		// script mounts it at /run/sd2; a folder standing in for it in the headless test: BANANA_SD2)
		struct Card { std::string id, label, root, roms; };	// id "data" or "sd2"; roms: <root>/roms
		bool mounted(const std::string& path)
		{
			std::ifstream f("/proc/mounts");
			std::string dev, at, rest;
			while (f >> dev >> at && std::getline(f, rest)) if (at == path) return true;
			return false;
		}
		std::vector<Card> cardsOf(const std::string& romRoot)
		{
			std::vector<Card> out;
			std::string root = romRoot.size() > 5 && romRoot.compare(romRoot.size() - 5, 5, "/roms") == 0 ? romRoot.substr(0, romRoot.size() - 5) : romRoot;
			out.push_back({ "data", "This card", root, romRoot });
			const char* t = getenv("BANANA_SD2");
			std::string sd2 = t && *t ? t : "/run/sd2";
			if ((t && *t) || mounted(sd2)) out.push_back({ "sd2", "Second SD card", sd2, sd2 + "/roms" });
			return out;
		}

		double steadyMs()
		{
			return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		std::string bsRun()
		{
			const char* e = getenv("BSHARE_RUN");	// (the headless test's)
			return e && *e ? e : "/run/banana";
		}

		std::string helper()
		{
			const char* e = getenv("BANANA_BSHARE");	// (a stand-in, for the headless test)
			return e && *e ? e : "/usr/bin/banana-bshare";
		}

		std::string readAll(const std::string& p)
		{
			std::ifstream f(p);
			std::stringstream s;
			s << f.rdbuf();
			return s.str();
		}

		std::vector<std::string> fields(const std::string& line)
		{
			std::vector<std::string> c;
			std::stringstream s(line);
			std::string x;
			while (std::getline(s, x, '\t')) c.push_back(x);
			return c;
		}

		pid_t servePid()
		{
			int p = atoi(readAll(bsRun() + "/bshare.pid").c_str());
			return p > 0 && kill(p, 0) == 0 ? p : 0;
		}

		// a program started on its own (setsid, a grandchild of nobody here: no zombie), its output dropped (it logs)
		void detached(const std::vector<std::string>& argv)
		{
			pid_t p = fork();
			if (p == 0)
			{
				setsid();
				if (fork() == 0)
				{
					int nul = ::open("/dev/null", O_RDWR);
					if (nul >= 0) { dup2(nul, 0); dup2(nul, 1); dup2(nul, 2); }
					std::vector<char*> a;
					for (auto& s : argv) a.push_back((char*)s.c_str());
					a.push_back(nullptr);
					execv(a[0], a.data());
					_exit(127);
				}
				_exit(0);
			}
			if (p > 0) while (waitpid(p, nullptr, 0) < 0 && errno == EINTR) {}
		}

		std::string files(long long n) { return std::to_string(n) + (n == 1 ? " file" : " files"); }

		long long countFiles(const std::string& dir, int depth = 0)
		{
			long long n = 0;
			DIR* d = opendir(dir.c_str());
			if (!d) return 0;
			while (dirent* e = readdir(d))
			{
				if (e->d_name[0] == '.') continue;
				std::string p = dir + "/" + e->d_name;
				struct stat st;
				if (stat(p.c_str(), &st) != 0) continue;
				if (S_ISDIR(st.st_mode)) { if (depth < 8) n += countFiles(p, depth + 1); }
				else n++;
			}
			closedir(d);
			return n;
		}
	}

	// Options' Receive when closed on (Ask or Quick save): the helper serving from the start, as the menu comes up; off:
	// stopped
	void Settings::bshareServe(bool want)
	{
		pid_t p = servePid();
		if (want && !p) detached({ helper(), "serve" });
		if (!want && p) kill(p, SIGTERM);
	}

	bool Settings::bshareReceiving() const
	{
		return getConfValue(data + "/settings.conf", "bshare_receive", "off") != "off";
	}

	// the files of a send, for its row: "Peel Quest.gba", "Saves", "3 files and folders"
	static std::string picked(const std::set<std::string>& m)
	{
		if (m.empty()) return "Choose";
		if (m.size() == 1) return baseName(*m.begin());
		return std::to_string(m.size()) + " files and folders";
	}

	struct Settings::Bshare
	{
		// home: Receive, Send, Options. receive: Accept and Decline (a device asking), Location. send: Device, Files,
		// Send. Under them: the folder for Location, the devices, the cards' files, a send's progress
		enum Sub { HOME, RECEIVE, LOCATION, SEND, DEVICES, FILES, STATUS, OPTIONS, SUBS };
		enum Kind { K_RECEIVE, K_SEND, K_OPTIONS, K_ACCEPT, K_DECLINE, K_LOCATION, K_DEVICE, K_FILES, K_GO, K_SENDING,
			K_BACKGROUND, K_ABOUT, K_HERE, K_ITEM, K_PEER, K_STOP, K_REQPIN, K_MYPIN };
		struct Row { std::string label, value, icon; Kind kind; int index = -1; bool choice = false, mark = false, warn = false; };
		struct Item { std::string name, path; bool dir = false; long long size = 0; };
		struct Peer { std::string fp, alias, model, type; };

		Settings* ui = nullptr;
		Sub sub = HOME;
		int sel[SUBS] = {}, top[SUBS] = {};
		std::string message;
		// a device asking to send (bshare-request), and the folder its files go to (empty: the usual places)
		std::string reqId, reqFrom, reqFirst, location;
		long long reqFiles = 0, reqBytes = 0;
		double reqSince = 0;
		std::string receiveLine;	// bshare-receive, as last read
		// the folders shown (Location's, Files'): "" the cards
		std::string dir;
		std::vector<Item> items;
		std::vector<Card> cards;
		// the send: the device and the files chosen, the helper sending
		std::vector<Peer> peers;
		std::string deviceFp, deviceName;
		std::set<std::string> marked;
		pid_t sender = 0;
		std::string sendLine, sendTo;
		// the last send's device and files, sent again with the PIN the device asked for (LocalSend's Require PIN)
		std::string lastFp;
		std::vector<std::string> lastFiles;
		bool pinAsked = false;
		double lookedAt = 0;

		std::string conf() const { return ui->data + "/settings.conf"; }
		std::string background() const { return getConfValue(conf(), "bshare_receive", "off"); }
		// Options' Require PIN (LocalSend's own): a device sending here must type this PIN (the helper checks it)
		bool pinRequired() const { return getConfValue(conf(), "bshare_require_pin", "off") == "on" && !myPin().empty(); }
		std::string myPin() const { return getConfValue(conf(), "bshare_pin", ""); }
		static const char* backgroundName(const std::string& v) { return v == "ask" ? "Ask" : v == "auto" ? "Quick save" : "Off"; }
		std::string name() const
		{
			std::string n = ui->dsName();
			if (!n.empty()) return n;
			std::string b = readAll("/etc/banana/board");
			return b.compare(0, 9, "rgds-plus") == 0 || b.empty() ? "RG DS Plus" : "RG DS";
		}
		std::string receivingMark() const { return bsRun() + "/bshare-receiving"; }

		void open()
		{
			sub = HOME;
			message.clear();
			cards = cardsOf(ui->romRoot);
			Settings::bshareServe(true);	// (seen while the app is open: to send, and to receive on its Receive page)
			poll(true);
		}

		void close()
		{
			unlink(receivingMark().c_str());
			location.clear();
			Settings::bshareServe(background() != "off");
		}

		// the Receive page open: the helper asks the menu for any request (Options' setting or not)
		void enter(Sub s)
		{
			if (s == RECEIVE) ::close(::open(receivingMark().c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0644));
			else if (s == HOME || s == SEND || s == OPTIONS) unlink(receivingMark().c_str());
			sub = s;
			message.clear();
		}

		// the helper's files read again: a request, the receive's and the send's state, the devices
		bool poll(bool force = false)
		{
			double t = steadyMs();
			if (!force && t - lookedAt < 400) return false;
			lookedAt = t;
			bool changed = false;
			std::string id, from, first;
			long long nf = 0, nb = 0;
			std::stringstream in(readAll(bsRun() + "/bshare-request"));
			std::string l;
			while (std::getline(in, l))
			{
				auto c = fields(l);
				if (c.size() < 2) continue;
				if (c[0] == "id") id = c[1]; else if (c[0] == "from") from = c[1]; else if (c[0] == "first") first = c[1];
				else if (c[0] == "files") nf = atoll(c[1].c_str()); else if (c[0] == "bytes") nb = atoll(c[1].c_str());
			}
			if (id != reqId)
			{
				reqId = id; reqFrom = from; reqFirst = first; reqFiles = nf; reqBytes = nb;
				reqSince = t;
				// a request: its page, at Accept (from the app's other pages too, but not mid-way through a folder)
				if (!id.empty() && sub != LOCATION && sub != FILES && sub != DEVICES) { enter(RECEIVE); sel[RECEIVE] = top[RECEIVE] = 0; }
				changed = true;
			}
			std::string rl = readAll(bsRun() + "/bshare-receive");
			if (rl != receiveLine) { receiveLine = rl; changed = true; }
			std::string sl = readAll(bsRun() + "/bshare-send");
			if (sl != sendLine) { sendLine = sl; changed = true; }
			if (sender > 0 && waitpid(sender, nullptr, WNOHANG) == sender) { sender = 0; changed = true; }
			// the device asks for a PIN (or the one typed was wrong): the keyboard, once a send
			if (sender == 0 && !pinAsked && !lastFiles.empty() && (sendLine.compare(0, 4, "pin\t") == 0 || sendLine.compare(0, 7, "badpin\t") == 0))
			{
				pinAsked = true;
				bool wrong = sendLine.compare(0, 7, "badpin\t") == 0;
				ui->startEntry(Settings::ENTRY_BSHARE_PIN, wrong ? "Wrong PIN for" : "PIN for", sendTo, Settings::BSHARE, "");
				ui->entryHelp = std::string(wrong ? "That PIN wasn't right. " : "") + sendTo +
					" asks for a PIN: it is in LocalSend there (Settings, Require PIN). START sends with it, B gives up.";
				changed = true;
			}
			std::vector<Peer> ps;
			std::stringstream pin(readAll(bsRun() + "/bshare-peers"));
			while (std::getline(pin, l))
			{
				auto c = fields(l);
				if (c.size() >= 7) ps.push_back({ c[0], c[4], c[5], c[6] });
			}
			if (ps.size() != peers.size() || !std::equal(ps.begin(), ps.end(), peers.begin(), [](const Peer& a, const Peer& b) { return a.fp == b.fp && a.alias == b.alias; }))
			{
				peers = ps;
				changed = true;
			}
			return changed;
		}

		void answer(bool yes)
		{
			std::string a = (yes ? "accept " : "decline ") + reqId + (yes && !location.empty() ? " " + location : "") + "\n";
			std::string p = bsRun() + "/bshare-answer", t = p + ".new";
			if (FILE* f = fopen(t.c_str(), "w")) { fputs(a.c_str(), f); fclose(f); rename(t.c_str(), p.c_str()); }
			message = yes ? "Receiving from " + reqFrom + "..." : "Declined: nothing was saved.";
			reqId.clear();
			sel[RECEIVE] = top[RECEIVE] = 0;
		}

		// ---- the folders -------------------------------------------------------------------------------------------
		void list(std::string d)	// (by value: d is often one of items' own paths, cleared below)
		{
			dir = d;
			items.clear();
			if (d.empty())
			{
				for (auto& c : cards) items.push_back({ c.label, c.root, true, 0 });
				sel[sub] = top[sub] = 0;
				return;
			}
			DIR* h = opendir(d.c_str());
			if (!h) { message = "That folder can't be read."; return; }
			while (dirent* e = readdir(h))
			{
				if (e->d_name[0] == '.') continue;
				Item it;
				it.name = e->d_name;
				it.path = d + "/" + it.name;
				struct stat st;
				if (stat(it.path.c_str(), &st) != 0) continue;
				it.dir = S_ISDIR(st.st_mode);
				it.size = st.st_size;
				if (!it.dir && sub == LOCATION) continue;	// (a location is a folder)
				items.push_back(it);
			}
			closedir(h);
			std::sort(items.begin(), items.end(), [](const Item& a, const Item& b)
			{
				if (a.dir != b.dir) return a.dir;
				return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
			});
			sel[sub] = top[sub] = 0;
		}

		std::string shown(const std::string& path) const
		{
			for (auto& c : cards)
				if (path.compare(0, c.root.size(), c.root) == 0) return c.label + (path.size() > c.root.size() ? path.substr(c.root.size()) : "");
			return path;
		}

		void up()
		{
			for (auto& c : cards) if (dir == c.root) { list(""); return; }
			list(parentPath(dir));
		}

		bool sending() const { return sender > 0 || sendLine.compare(0, 7, "sending") == 0 || sendLine.compare(0, 7, "waiting") == 0; }
		bool deviceThere() const { for (auto& p : peers) if (p.fp == deviceFp) return true; return false; }

		static std::string kindOf(const Peer& p)
		{
			return p.type == "desktop" || p.type == "laptop" ? "Computer" : p.type == "mobile" && p.model.compare(0, 5, "RG DS") == 0 ? "Handheld" :
				p.type == "mobile" ? "Phone" : p.type == "web" ? "Browser" : "Device";
		}

		// ---- the rows ----------------------------------------------------------------------------------------------
		std::vector<Row> rows() const
		{
			std::vector<Row> r;
			auto add = [&](const std::string& label, const std::string& value, Kind k) -> Row& { r.push_back({ label, value, "", k }); return r.back(); };
			switch (sub)
			{
			case HOME:
				add("Receive", "", K_RECEIVE).icon = "bshare-receive";
				add("Send", sending() ? "Sending" : "", K_SEND).icon = "bshare-send";
				add("Options", "", K_OPTIONS);
				break;
			case RECEIVE:
				if (!reqId.empty())
				{
					add("Accept", "", K_ACCEPT).icon = "bshare-receive";
					add("Decline", "", K_DECLINE);
				}
				add("Location", location.empty() ? "Usual places" : shown(location), K_LOCATION);
				break;
			case LOCATION:
				if (!dir.empty()) add("This folder", "", K_HERE).icon = "bshare-receive";
				for (size_t i = 0; i < items.size(); i++) add(items[i].name, "Folder", K_ITEM).index = (int)i;
				break;
			case SEND:
			{
				std::string dev = deviceFp.empty() ? "Choose" : deviceThere() ? deviceName : deviceName + " (gone)";
				add("Device", dev, K_DEVICE).warn = !deviceFp.empty() && !deviceThere();
				add("Files", picked(marked), K_FILES);
				add("Send", "", K_GO).icon = "bshare-send";
				if (sending()) add("Sending", "", K_SENDING);
				break;
			}
			case DEVICES:
				for (size_t i = 0; i < peers.size(); i++)
				{
					std::string k = kindOf(peers[i]);
					Row& x = add(peers[i].alias, k, K_PEER);
					x.index = (int)i;
					x.icon = k == "Computer" ? "bshare-computer" : k == "Handheld" ? "bshare-handheld" : "bshare-phone";
				}
				break;
			case FILES:
				for (size_t i = 0; i < items.size(); i++)
				{
					Row& x = add(items[i].name, items[i].dir ? "Folder" : bytes(items[i].size), K_ITEM);
					x.index = (int)i;
					x.mark = marked.count(items[i].path) > 0;
				}
				break;
			case STATUS:
				if (sender > 0) add("Stop", "", K_STOP);
				break;
			case OPTIONS:
				add("Receive when closed", backgroundName(background()), K_BACKGROUND).choice = true;
				add("Require PIN", pinRequired() ? "On" : "Off", K_REQPIN).choice = true;
				if (pinRequired()) add("PIN", myPin(), K_MYPIN);
				add("About Bshare", "", K_ABOUT);
				break;
			default:
				break;
			}
			return r;
		}

		Result activate(const Row& row)
		{
			switch (row.kind)
			{
			case K_RECEIVE: enter(RECEIVE); sel[RECEIVE] = top[RECEIVE] = 0; return MOVED;
			case K_SEND: enter(SEND); sel[SEND] = top[SEND] = 0; return MOVED;
			case K_OPTIONS: enter(OPTIONS); return MOVED;
			case K_ACCEPT: answer(true); return CHANGED;
			case K_DECLINE: answer(false); return CHANGED;
			case K_LOCATION: sub = LOCATION; message.clear(); list(""); return MOVED;
			case K_HERE: location = dir; sub = RECEIVE; message.clear(); return MOVED;
			case K_DEVICE: sub = DEVICES; message.clear(); sel[DEVICES] = top[DEVICES] = 0; return MOVED;
			case K_FILES: sub = FILES; message.clear(); list(""); return MOVED;
			case K_GO: return send();
			case K_SENDING: sub = STATUS; message.clear(); return MOVED;
			case K_BACKGROUND: return change(1);
			case K_REQPIN: return requirePin(!pinRequired());
			case K_MYPIN:
				ui->startEntry(Settings::ENTRY_BSHARE_MYPIN, "Bshare", "Your PIN", Settings::BSHARE, myPin());
				ui->entryHelp = "The PIN a phone or computer types to send here: 4 to 8 digits. START keeps it.";
				return MOVED;
			case K_ABOUT: return NONE;
			case K_ITEM:
			{
				const Item& it = items[row.index];
				if (it.dir) { list(it.path); return MOVED; }
				if (sub == FILES) { if (!marked.erase(it.path)) marked.insert(it.path); return MOVED; }	// (A on a file: picked)
				return NONE;
			}
			case K_PEER:
				deviceFp = peers[row.index].fp;
				deviceName = peers[row.index].alias;
				sub = SEND;
				message.clear();
				return MOVED;
			case K_STOP:
				if (sender > 0) kill(sender, SIGTERM);
				return CHANGED;
			}
			return NONE;
		}

		// the PIN typed for the last send: the same files to the same device again, with it ("": given up); or Options'
		// own PIN
		Result entered(const std::string& pin, bool mine)
		{
			if (mine) return setMyPin(pin);
			if (pin.empty() || lastFp.empty() || lastFiles.empty()) { message = "Nothing was sent."; return MOVED; }
			deviceFp = lastFp;
			marked = std::set<std::string>(lastFiles.begin(), lastFiles.end());
			return send(pin);
		}

		// Send (or START on the Send page): the device and the files chosen, then the helper sends them (with the
		// PIN the device asked for)
		Result send(const std::string& pin = "")
		{
			if (sending()) { message = "A send is under way: Sending shows it."; return MOVED; }
			if (deviceFp.empty() || (pin.empty() && !deviceThere())) { message = "Choose a device first: a phone or computer with LocalSend open."; sel[SEND] = 0; return MOVED; }
			if (marked.empty()) { message = "Choose the files first."; sel[SEND] = 1; return MOVED; }
			std::vector<std::string> a = { helper(), "send" };
			if (!pin.empty()) { a.push_back("--pin"); a.push_back(pin); }
			a.push_back(deviceFp);
			for (auto& w : marked) a.push_back(w);
			lastFp = deviceFp;
			lastFiles.assign(marked.begin(), marked.end());
			pinAsked = false;
			sendTo = deviceName;
			unlink((bsRun() + "/bshare-send").c_str());
			sendLine.clear();
			pid_t p = fork();
			if (p == 0)
			{
				int nul = ::open("/dev/null", O_RDWR);
				if (nul >= 0) { dup2(nul, 0); dup2(nul, 1); dup2(nul, 2); }
				std::vector<char*> v;
				for (auto& s : a) v.push_back((char*)s.c_str());
				v.push_back(nullptr);
				execv(v[0], v.data());
				_exit(127);
			}
			sender = p > 0 ? p : 0;
			marked.clear();
			sub = STATUS;
			message.clear();
			return MOVED;
		}

		// Require PIN on (a random 4-digit PIN if there is none yet: the PIN row shows and changes it) or off
		Result requirePin(bool on)
		{
			if (on && myPin().empty())
			{
				std::random_device rd;
				char b[8];
				snprintf(b, sizeof b, "%04u", (unsigned)(rd() % 10000));
				setConfValue(conf(), "bshare_pin", b);
			}
			setConfValue(conf(), "bshare_require_pin", on ? "on" : "off");
			return CHANGED;
		}

		// the PIN typed on Options' PIN row: digits only, 4 to 8 ("" or anything else: kept as it was)
		Result setMyPin(const std::string& t)
		{
			bool ok = t.size() >= 4 && t.size() <= 8 && std::all_of(t.begin(), t.end(), [](char c) { return c >= '0' && c <= '9'; });
			if (ok) setConfValue(conf(), "bshare_pin", t);
			message = ok ? "" : t.empty() ? "" : "A PIN is 4 to 8 digits: kept as it was.";
			return CHANGED;
		}

		Result change(int d)
		{
			static const char* const V[] = { "off", "ask", "auto" };
			std::string v = background();
			int i = v == "ask" ? 1 : v == "auto" ? 2 : 0;
			setConfValue(conf(), "bshare_receive", V[(i + d + 3) % 3]);
			return CHANGED;	// (the helper reads it at each request; closing the app starts or stops it)
		}

		Result back()
		{
			switch (sub)
			{
			case HOME: ui->close(); return CLOSED;
			case LOCATION:
				if (dir.empty()) { sub = RECEIVE; message.clear(); return MOVED; }
				up();
				return MOVED;
			case FILES:
				if (dir.empty()) { sub = SEND; message.clear(); return MOVED; }
				up();
				return MOVED;
			case DEVICES: case STATUS: sub = SEND; message.clear(); return MOVED;
			default: enter(HOME); return MOVED;
			}
		}

		Result press(Key k)
		{
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
				return MOVED;
			};
			if (k != Key::NONE && sub != STATUS) message.clear();
			switch (k)
			{
			case Key::UP: return moveTo(s - 1);
			case Key::DOWN: return moveTo(s + 1);
			case Key::L: if (!n) return NONE; s = std::max(0, s - ROWS); t = std::max(0, t - ROWS); if (s < t) t = s; return MOVED;
			case Key::R: if (!n) return NONE; s = std::min(n - 1, s + ROWS); if (s >= t + ROWS) t = s - ROWS + 1; return MOVED;
			case Key::LEFT: case Key::RIGHT:
				if (cur && cur->choice && cur->kind == K_REQPIN) return requirePin(!pinRequired());	// (two values: either way flips it)
				if (cur && cur->choice) return change(k == Key::LEFT ? -1 : 1);
				if (k == Key::LEFT && sub != HOME) return back();
				return NONE;
			case Key::B: return back();
			case Key::A: return cur ? activate(*cur) : NONE;
			case Key::Y:
				// Files: a folder (or a file) marked to go as it is
				if (sub == FILES && cur && cur->kind == K_ITEM && !dir.empty())
				{
					const std::string& p = items[cur->index].path;
					if (!marked.erase(p)) marked.insert(p);
					return moveTo(s + 1);
				}
				return NONE;
			case Key::START:
				if (sub == SEND) return send();	// (Send's confirmation, as its row)
				if (sub == FILES) { sub = SEND; message.clear(); return MOVED; }	// (done choosing)
				return NONE;
			default:
				return NONE;
			}
		}

		Result touch(float cx, float cy)
		{
			float dx = ui->downX, dy = ui->downY;
			std::vector<Row> r = rows();
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
				if (sub == FILES && dx < 70 && !dir.empty()) { s = at; return press(Key::Y); }	// (the tick's place)
				if (at != s) { s = at; return MOVED; }
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

		// ---- the top panel -----------------------------------------------------------------------------------------
		struct Top { std::string label = "Bshare", title, accent, help, hint; double a = -1; std::string aLabel; };

		Top topPanel(const std::vector<Row>& r) const
		{
			Top t;
			const Row* cur = sel[sub] < (int)r.size() ? &r[sel[sub]] : nullptr;
			auto rc = fields(receiveLine.substr(0, receiveLine.find('\n')));
			if (cur) { t.title = cur->label; t.accent = cur->value; }
			switch (sub)
			{
			case HOME:
				if (!cur) break;
				if (cur->kind == K_RECEIVE) { t.help = "Get files from a phone or computer with LocalSend."; t.hint = "Seen as " + name() + " while this page is open."; }
				if (cur->kind == K_SEND) t.help = "Send files or folders to a phone or computer with LocalSend open.";
				if (cur->kind == K_OPTIONS) t.help = "Receiving while Bshare is closed, and about Bshare.";
				break;
			case RECEIVE:
				t.label = "Receive";
				if (!reqId.empty())
				{
					int left = std::max(0, 60 - (int)((steadyMs() - reqSince) / 1000));
					t.title = reqFrom + " wants to send";
					t.accent = files(reqFiles) + ", " + bytes(reqBytes) + "  ·  " + std::to_string(left) + " s";
					t.help = "First: " + reqFirst + ".";
					t.hint = cur && cur->kind == K_DECLINE ? "Nothing is saved." : !location.empty() ? "Everything goes into " + shown(location) + "." :
						"Games go into their system's folder, everything else into banana/received.";
				}
				else
				{
					t.title = "Ready";
					t.accent = "Seen as " + name() + (pinRequired() ? "  ·  PIN " + myPin() : "");
					t.help = "Open LocalSend on your phone or computer, on the same Wi-Fi, and send to " + name() + "." +
						(pinRequired() ? " It asks for your PIN, " + myPin() + "." : "");
					t.hint = !location.empty() ? "Files go into " + shown(location) + "." : "Games go into their system's folder, everything else into banana/received. Location changes that.";
					if (rc.size() >= 6 && rc[0] == "receiving")
					{
						t.title = "Receiving from " + rc[1];
						t.accent = rc[2] + " of " + rc[3] + " files";
						long long tot = atoll(rc[5].c_str());
						if (tot > 0) { t.a = (double)atoll(rc[4].c_str()) / tot; t.aLabel = bytes(atoll(rc[4].c_str())) + " of " + bytes(tot); }
						t.hint.clear();
					}
					else if (rc.size() >= 4 && (rc[0] == "done" || rc[0] == "partly")) t.hint = "Last: " + files(atoll(rc[2].c_str())) + " from " + rc[1] + ".";
				}
				break;
			case LOCATION:
				t.label = "Location";
				t.title = dir.empty() ? "The cards" : shown(dir);
				t.accent.clear();
				t.help = "A opens a folder. This folder: everything received goes here.";
				break;
			case SEND:
				t.label = "Send";
				if (cur && cur->kind == K_DEVICE) t.help = "A phone or computer with LocalSend open, on the same Wi-Fi.";
				if (cur && cur->kind == K_FILES) t.help = marked.empty() ? "Files or folders on the cards." : std::to_string(marked.size()) + " chosen. A changes them.";
				if (cur && cur->kind == K_GO)
				{
					t.help = !deviceFp.empty() && !marked.empty() ? picked(marked) + " to " + deviceName + ". It asks there before taking them." : "Choose a device and the files first.";
					t.accent.clear();
				}
				if (cur && cur->kind == K_SENDING) t.help = "A shows it.";
				break;
			case DEVICES:
				t.label = "Device";
				t.title = cur ? cur->label : "Looking for devices";
				t.help = "Phones and computers with LocalSend open, on the same Wi-Fi.";
				break;
			case FILES:
				t.label = "Files";
				if (!cur) t.title = dir.empty() ? "The cards" : shown(dir);
				t.help = "A opens a folder or picks a file. Y picks a whole folder. START: done.";
				if (!marked.empty()) t.hint = std::to_string(marked.size()) + " chosen.";
				break;
			case STATUS:
			{
				auto c = fields(sendLine.substr(0, sendLine.find('\n')));
				t.label = "Send";
				std::string who = c.size() >= 2 ? c[1] : sendTo;
				std::string what = c.empty() ? (sender > 0 ? "waiting" : "") : c[0];
				t.title = what == "pin" ? who + " asks for a PIN" : what == "badpin" ? "Wrong PIN for " + who :
					what == "waiting" ? "Waiting for " + who : what == "sending" ? "Sending to " + who : what == "done" ? "Sent to " + who :
					what == "declined" ? who + " said no" : what == "busy" ? who + " is busy" : what == "stopped" ? "Stopped" : what == "partly" ? "Partly sent" : what.empty() ? "" : "It didn't work";
				t.accent = c.size() >= 6 ? c[2] + " of " + c[3] + " files" : "";
				t.help = what == "pin" || what == "badpin" ? "The PIN is in LocalSend there (Settings, Require PIN). Send again to type it." :
					what == "waiting" ? "It asks there first." : what == "sending" ? "You can leave: it carries on." :
					what == "done" ? "All the files arrived." : what == "declined" ? "Nothing was sent." : what == "error" ? "The device couldn't be reached. Is LocalSend open, on the same Wi-Fi?" : "";
				if (c.size() >= 6 && atoll(c[5].c_str()) > 0)
				{
					t.a = (double)atoll(c[4].c_str()) / (double)atoll(c[5].c_str());
					t.aLabel = bytes(atoll(c[4].c_str())) + " of " + bytes(atoll(c[5].c_str()));
				}
				break;
			}
			case OPTIONS:
				t.label = "Options";
				if (cur && cur->kind == K_BACKGROUND)
				{
					std::string v = background();
					t.help = "Whether devices can send to " + name() + " while Bshare is closed.";
					t.hint = v == "ask" ? "A notification asks; Bshare's Receive page answers." : v == "auto" ? "Files are saved without asking: only on a network you trust." :
						"Only while Bshare's Receive page is open.";
				}
				if (cur && cur->kind == K_REQPIN)
				{
					t.help = "Whether a phone or computer must type your PIN before it can send to " + name() + ", as LocalSend's own Require PIN.";
					t.hint = pinRequired() ? "On: your PIN is " + myPin() + ". The PIN row changes it." : "Off: anyone on the Wi-Fi can ask to send (you still accept each one).";
				}
				if (cur && cur->kind == K_MYPIN)
					t.help = "Your PIN, " + myPin() + ": LocalSend asks for it on the phone or computer that sends here. A changes it.";
				if (cur && cur->kind == K_ABOUT)
					t.help = "Bshare works with LocalSend, the free and open-source app by Tien Do Nam and its contributors (localsend.org). "
						"It is Banana's own small version of LocalSend's open protocol: get LocalSend on your phone or computer, on the same Wi-Fi.";
				break;
			default: break;
			}
			if (!message.empty()) t.help = message;
			return t;
		}

		std::string header() const
		{
			switch (sub)
			{
			case HOME: return "Bshare";
			case RECEIVE: return "Receive";
			case LOCATION: return dir.empty() ? "Location" : shown(dir);
			case SEND: return "Send";
			case DEVICES: return "Device";
			case FILES: return dir.empty() ? "Files" : shown(dir);
			case STATUS: return "Sending";
			case OPTIONS: return "Options";
			default: return "";
			}
		}

		std::string emptyText() const
		{
			if (sub == DEVICES) return "Looking... Open LocalSend on the other device.";
			if (sub == FILES || sub == LOCATION) return "An empty folder.";
			return "";
		}

		std::vector<Settings::Chip> chips(const std::vector<Row>& r) const
		{
			const Row* cur = sel[sub] < (int)r.size() ? &r[sel[sub]] : nullptr;
			std::vector<Settings::Chip> cs = { { Key::B, "B", sub == HOME ? "Close" : "Back" } };
			std::string a;
			switch (sub)
			{
			case HOME: case OPTIONS:
				a = !cur ? "" : cur->kind == K_BACKGROUND || cur->kind == K_REQPIN || cur->kind == K_MYPIN ? "Change" : cur->kind == K_ABOUT ? "" : "Open";
				break;
			case RECEIVE: a = !cur ? "" : cur->kind == K_ACCEPT ? "Accept" : cur->kind == K_DECLINE ? "Decline" : "Choose"; break;
			case LOCATION: a = !cur ? "" : cur->kind == K_HERE ? "Choose" : "Open"; break;
			case SEND:
				cs.push_back({ Key::START, "START", "Send" });
				a = !cur ? "" : cur->kind == K_GO ? "Send" : cur->kind == K_SENDING ? "Open" : "Choose";
				break;
			case DEVICES: a = cur ? "Choose" : ""; break;
			case FILES:
				if (!dir.empty()) cs.push_back({ Key::Y, "Y", "Pick folder" });
				cs.push_back({ Key::START, "START", "Done" });
				a = !cur ? "" : items[cur->index].dir ? "Open" : marked.count(items[cur->index].path) ? "Unpick" : "Pick";
				break;
			case STATUS: a = cur ? "Stop" : ""; break;
			default: break;
			}
			if (!a.empty()) cs.push_back({ Key::A, "A", a, true });
			return cs;
		}

		// ---- drawing -----------------------------------------------------------------------------------------------
		void render()
		{
			SPal p = pal(ui->light);
			std::vector<Row> r = rows();
			Top t = topPanel(r);
			rnds::Backend* be = ui->be;
			be->beginScreen(0);
			ui->box("screen", false, 0, 0, 640, 480);
			ui->box("hero", false, 18, 20, 604, 436);
			ui->icon("bshare", 50, 56, 3);
			ui->text(ui->fit(t.label, true, 17, 420), true, 17, 170, 62, p.muted, true, 1.4f);
			ui->text(ui->fit(t.title.empty() ? " " : t.title, true, 30, 420), true, 30, 170, 88, p.ink);
			if (!t.accent.empty()) ui->text(ui->fit(t.accent, true, 22, 420), true, 22, 170, 130, p.accent);
			ui->wrapped(t.help, 20, 50, 196, 540, t.hint.empty() && t.a < 0 ? 10 : 3, message.empty() ? p.muted : p.ink);
			if (!t.hint.empty()) ui->wrapped(t.hint, 20, 50, Settings::HINT_TOP, 540, 5, p.ink);
			if (t.a >= 0)
			{
				ui->meter(50, 316, 540, t.a);
				ui->text(ui->fit(t.aLabel, false, 16, 540), false, 16, 50, 338, p.muted);
			}
			be->endScreen();

			be->beginScreen(1);
			ui->box("screen", false, 0, 0, 640, 480);
			ui->box("bubble", false, 18, 14, 604, 54);
			std::string count = r.empty() ? "" : std::to_string(sel[sub] + 1) + " / " + std::to_string(r.size());
			if (sub == FILES && !marked.empty()) count = std::to_string(marked.size()) + " chosen";
			float cw = ui->text(count, true, 17, 0, 0, p.muted, false, 0, true);
			ui->text(ui->fit(header(), true, 22, 540 - cw), true, 22, 38, 28, p.ink);
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
				float lx = 38;
				if (row.mark) { ui->tick(38, y + 21, on ? p.onPick : p.accent); lx = 62; }
				else if (!row.icon.empty()) { ui->icon(row.icon, 34, y + 12, 2); lx = 76; }
				float vw = row.value.empty() ? 0 : ui->text(row.value, true, 19, 0, 0, vc, false, 0, true);
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

		bool update(double) { return poll(); }

		// the files looked at twice a second while the app is open: a request's countdown, a transfer's progress
		int wake(double) const { return 500; }
	};

	void Settings::openBshare()
	{
		if (!bs)
		{
			bs = std::make_shared<Bshare>();
			bs->ui = this;
		}
		mOpen = true;
		page = BSHARE;
		bs->open();
	}

	void Settings::closeBshare() { if (bs) bs->close(); }
	Settings::Result Settings::finishBshareEntry()
	{
		std::string text = pw;
		pw.clear();
		page = BSHARE;
		message.clear();
		return bs ? bs->entered(text, entry == ENTRY_BSHARE_MYPIN) : MOVED;
	}

	Settings::Result Settings::pressBshare(Key k) { return bs ? bs->press(k) : NONE; }
	Settings::Result Settings::touchBshare(float cx, float cy) { return bs ? bs->touch(cx, cy) : NONE; }
	void Settings::renderBshare() { if (bs) bs->render(); }
	bool Settings::updateBshare(double t) { return bs && bs->update(t); }
	int Settings::bshareWake(double t) const { return bs && page == BSHARE ? bs->wake(t) : -1; }

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
}
