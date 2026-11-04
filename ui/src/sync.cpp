// banana-sync: the card kept in step with a home server: its games (with their box art and gamelist entries) copied
// down, and the saves synced both ways. Two kinds of server, behind one Backend:
//   Romhost  the ROM library on Homeport (romhost/PROTOCOL.md: UDP discovery, a 6-digit pairing code, a manifest, a
//            save plan the server decides)
//   RomM     the popular self-hosted ROM manager (rommapp/romm, API of 5.x: a device pairing code approved in its web
//            UI, platforms + roms listed, ROMs and covers downloaded, saves and states per ROM)
// Romhost, the ROM sync, pairing and the commands are here; RomM is in sync_romm.cpp, and what they share (http(),
// the state file, Backend, the saves on the card) in sync_core.h.
// Usage (the menu runs it; it says how far it got in /run/banana/sync.status, one line):
//   banana-sync discover                       Romhost servers on the LAN: "ROMHOST\t<url>\t<name>" lines
//   banana-sync probe <address>                what answers there: "romhost <url> <name>" / "romm <url> <version>" / "none";
//                                              an address with no scheme is tried on HTTPS first
//   banana-sync pair <address> [code]          Romhost: the code from Homeport. RomM: no code = the device flow (the
//                                              status shows the code to type in RomM: "pair <code> <url>"); a code =
//                                              a client token's pairing code; rmm_... = a token pasted. Whichever,
//                                              the card is then a RomM device: bananaos-<banana/sync-id>, kept
//   banana-sync forget                         the pairing (and the sync's state) dropped; the files stay
//   banana-sync sync [--saves-only] [--rom <path>] [--auto quit|wifi|launch] [--dry-run]
//                                              --rom: that one game's save and, with homesync_states, its states (its
//                                              ROM's path, roms-relative or absolute, on either SD card), beside a sync
//                                              that is copying games: one at a time over the saves only, its status in
//                                              <run>/gameend.status. Romhost and a RomM before 5.4 sync every save
//                                              instead;
//                                              --auto launch --rom: the one before the game starts (homesync_launch),
//                                              the save alone, each request given less time, its status in
//                                              <run>/launch.status; RomM 5.4 and newer only
//   banana-sync status                         the pairing and the last sync, for a person
//   banana-sync blank <file> [<folder>]        exit 0 when the save is blank (a core's fresh one); folder: under saves/
// The card: --root /data (saves/, states/, drastic/), --roms <root>/roms, --conf <root>/banana/romsync.json,
// --settings <root>/banana/settings.conf (homesync_roms, homesync_roms_to, homesync_states, homesync_auto,
// homesync_launch, homesync.<system>, homesync_ca: a PEM file of the CA that signed an https server's certificate,
// homesync_tls = off: no certificate checked).
// The second SD card's roms folder (homesync_roms_to = sd2) is the one <run>/sd2/roms names, as the library's.
// Each sync's report: <root>/banana/logs/sync.log (the one before: sync.log.1).
// Rules kept (PROTOCOL.md "What a device sync must do"): only files the sync put there are ever changed or deleted
// (the state lists them with their hashes; a file of the user's at the same path is left alone and reported);
// downloads go to <file>.part, are checked, then renamed; gamelist.xml is merged; saves never sync while a game runs.
// A save comes down to /run first, is checked against the server's hash and installed through the vault (the save it
// replaces kept, the install journalled: save_vault.h), after the journal of a run cut short is recovered; a blank
// save is neither sent nor taken (save_adapt.h). RomM's saves are each game's one save file, the one its emulator
// reads, for a game whose ROM is the server's by its contents (save_paths.h); a RomM before 5.4 only gives saves, and
// one that has this handheld's sync switched off gives none.
#include "json.h"
#include "save_adapt.h"
#include "save_vault.h"
#include "sync_core.h"
#include "sync_romm.h"
#include "sync_util.h"
#include <arpa/inet.h>
#include <cerrno>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <signal.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

using namespace bsync;
using banana::JVal;
using banana::JPtr;

namespace
{
	// =============================================================================================================
	// Romhost (romhost/PROTOCOL.md)
	// =============================================================================================================
	struct Romhost : Backend
	{
		using Backend::Backend;
		bool manifest(Manifest& m, const std::set<std::string>& skip, std::string& err) override
		{
			std::vector<std::string> h;
			if (!c.etag.empty()) h.push_back("If-None-Match: " + c.etag);
			Resp r = call("GET", "/api/v1/manifest", nullptr, h);
			if (r.code == 304) { m.unchanged = true; return true; }
			if (r.code != 200) { err = r.code == 401 ? "unpaired" : r.error(); return false; }
			JPtr j = r.json();
			if (!j || !j->isObj()) { err = "the server's list can't be read"; return false; }
			m.generation = (long long)j->num("generation");
			m.etag = r.hdr["etag"];
			if (const JVal* ss = j->get("systems"))
				for (auto& s : ss->arr)
				{
					MSystem sys;
					sys.folder = s->str("folder");
					if (!safeRel(sys.folder) || skip.count(s->str("id"))) continue;
					if (const JVal* gs = s->get("games"))
						for (auto& g : gs->arr)
						{
							MGame game;
							game.title = g->str("title");
							bool ok = true;
							if (const JVal* fs = g->get("files"))
								for (auto& f : fs->arr)
								{
									MFile mf;
									mf.path = f->str("path");
									mf.size = (long long)f->num("size", -1);
									mf.sha1 = f->str("sha1");
									mf.media = f->str("kind") == "media";
									if (mf.media && ownCovers.count(sys.folder))
									{
										continue;	// (a system that makes its own covers)
									}
									mf.url = c.server + "/api/v1/files/" + pathEsc(mf.path);
									if (!safeRel(mf.path) || mf.path.compare(0, sys.folder.size() + 1, sys.folder + "/") != 0) { ok = false; break; }
									game.files.push_back(mf);
								}
							if (const JVal* gl = g->get("gamelist"))
								for (auto& kv : gl->obj)
									game.gl.push_back({ kv.first, kv.second->isNum() ? fmtNum(kv.second->n) : kv.second->s });
							if (ok) sys.games.push_back(game); else m.skipped++;
						}
					m.systems.push_back(sys);
				}
			return true;
		}
		void report(const Manifest& m, int downloaded, int removed, const std::vector<std::string>& conflicts, const std::vector<std::string>& errors) override
		{
			std::string b = "{\"generation\": " + std::to_string(errors.empty() ? m.generation : 0) + ", \"downloaded\": " + std::to_string(downloaded) +
				", \"removed\": " + std::to_string(removed) + ", \"conflicts\": [";
			for (size_t i = 0; i < conflicts.size() && i < 50; i++) b += (i ? ", " : "") + jsonStr(conflicts[i]);
			b += "], \"errors\": [";
			for (size_t i = 0; i < errors.size() && i < 50; i++) b += (i ? ", " : "") + jsonStr(errors[i]);
			b += "], \"free\": " + std::to_string(freeBytes(ROMS)) + ", \"files\": " + std::to_string(c.files.size()) + ", \"client\": " + jsonStr(VERSION) + "}";
			call("POST", "/api/v1/report", &b);
		}
		// PROTOCOL.md "Saves": the plan, then each action; the server decides conflicts and keeps every version
		bool saves(bool, int& up, int& down, int& conflicts, std::vector<std::string>& errors, std::string& note) override
		{
			Resp r = call("GET", "/api/v1/saves/rules");
			if (r.code != 200) { errors.push_back("saves: " + (r.code == 401 ? std::string("unpaired") : r.error())); return false; }
			JPtr rules = r.json();
			std::string mode = rules ? rules->str("mode", "off") : "off";
			if (mode == "off") { note = "save sync is off for this handheld (Homeport: ROMs > Devices)"; return true; }
			ndsSavesFirst();
			auto local = localSaves(mode == "states");
			std::map<std::string, std::string> sums;
			std::string b = "{\"files\": [";
			bool first = true;
			for (auto& kv : local)
			{
				std::string p = ROOT + "/" + kv.first;
				sums[kv.first] = hashFile(p, "sha1");
				if (sums[kv.first].empty()) continue;
				auto st = c.saves.find(kv.first);
				// a core's fresh save that never synced: left out, so the server plans its own version down over it
				// (listed, it would be a conflict with no base). One that did sync stays listed: not taken as deleted
				if (st == c.saves.end() && blankSave(readFile(p), saveFolder(kv.first))) continue;
				char mt[32]; snprintf(mt, sizeof mt, "%.3f", fileTime(p));
				b += std::string(first ? "" : ", ") + "{\"path\": " + jsonStr(kv.first) + ", \"sha1\": " + jsonStr(sums[kv.first]) + ", \"size\": " +
					std::to_string(fileSize(p)) + ", \"mtime\": " + mt + ", \"base\": " + (st == c.saves.end() ? "0" : fmtNum(st->second.version)) +
					", \"base_sha1\": " + jsonStr(st == c.saves.end() ? "" : st->second.hash) + "}";
				first = false;
			}
			b += "], \"deleted\": [";
			first = true;
			for (auto& kv : c.saves)
				if (!local.count(kv.first)) { b += std::string(first ? "" : ", ") + "{\"path\": " + jsonStr(kv.first) + ", \"base\": " + fmtNum(kv.second.version) + "}"; first = false; }
			b += "]}";
			r = call("POST", "/api/v1/saves/plan", &b);
			JPtr plan = r.code == 200 ? r.json() : nullptr;
			if (!plan || !plan->get("actions")) { errors.push_back("saves: " + r.error()); return false; }
			int n = 0, total = (int)plan->get("actions")->arr.size();
			for (auto& a : plan->get("actions")->arr)
			{
				if (stopForGame(errors)) break;
				std::string rel = a->str("path"), act = a->str("action");
				n++;
				if (!safeRel(rel) || saveKind(rel).empty()) continue;
				// (the DS's two saves both changed since they last agreed: the game left alone until one is chosen; still
				// in the plan's list, so the server doesn't take it as deleted)
				if (ndsDivergedSave(rel)) { say("  not synced, DraStic's and BananaDS's saves both changed since they last agreed: %s", rel.c_str()); continue; }
				status("running " + std::to_string(n) + " " + std::to_string(total) + " Saves|" + baseOf(rel));
				std::string p = ROOT + "/" + rel;
				if (act == "same") c.saves[rel] = { a->num("version"), a->str("sha1"), "" };
				else if (act == "forget") c.saves.erase(rel);
				else if ((act == "upload" || act == "conflict") && !a->flag("waiting"))
				{
					if (!local.count(rel)) continue;
					std::string body = readFile(p);
					if (hashStr(body, "sha1") != sums[rel]) { errors.push_back(rel + ": changed while syncing, next time"); continue; }
					// (a core's fresh save: never sent; still in the plan's list, so the server doesn't take it as deleted)
					if (blankSave(body, saveFolder(rel))) { say("  blank, not sent: %s", rel.c_str()); continue; }
					std::string qs = act == "upload" ? "expect=" + fmtNum(a->num("expect")) : "conflict=1&base=" + (c.saves.count(rel) ? fmtNum(c.saves[rel].version) : std::string("0"));
					char mt[48]; snprintf(mt, sizeof mt, "X-Save-Mtime: %.3f", fileTime(p));
					if (dryRun) { say("  upload %s", rel.c_str()); continue; }
					Req q; q.method = "PUT"; q.url = c.server + "/api/v1/saves/file/" + pathEsc(rel) + "?" + qs; q.headers = auth();
					q.headers.push_back("X-Save-Sha1: " + sums[rel]); q.headers.push_back(mt); q.headers.push_back("Content-Type: application/octet-stream");
					q.body = &body; q.timeout = 0;
					Resp u = http(q);
					if (u.code != 200) { errors.push_back(rel + ": " + u.error()); continue; }
					vaultGood(rel, body);
					if (act == "upload") { JPtr res = u.json(); c.saves[rel] = { res ? res->num("version") : 0, sums[rel], "" }; up++; say("  up %s", rel.c_str()); }
					else { conflicts++; say("  conflict (kept here; Homeport decides) %s", rel.c_str()); }
				}
				else if (act == "conflict") conflicts++;
				else if (act == "download")
				{
					if (exists(p) && hashFile(p, "sha1") != sums[rel]) { errors.push_back(rel + ": changed while syncing, next time"); continue; }
					if (dryRun) { say("  download %s", rel.c_str()); continue; }
					std::string err, dl = RUN + "/sync-dl." + std::to_string(getpid());
					Install in = fetchTo(c.server + "/api/v1/saves/file/" + pathEsc(rel) + "?version=" + fmtNum(a->num("version")), auth(), dl, -1, "sha1", a->str("sha1"), err)
						? installSave(rel, dl, fmtNum(a->num("version")), err) : Install::Failed;
					if (in == Install::Blank) { say("  blank on the server, not taken: %s", rel.c_str()); continue; }
					if (in == Install::Failed) { dropDownload(dl); errors.push_back(rel + ": " + err); continue; }
					// (DraStic's .dsv brought into step with a DS save just installed, before either emulator starts)
					if (saveFolder(rel) == "nds") ndsSaves(ROMS + "/nds/" + stemOf(baseOf(rel)) + ".nds");
					// (a game started while it was placed: not recorded, so the next sync decides again from what it finds)
					if (gameRunning()) { errors.push_back(rel + ": a game started while it was placed, decided next time"); continue; }
					c.saves[rel] = { a->num("version"), a->str("sha1"), "" };
					down++;
					say("  down %s", rel.c_str());
				}
				saveConf(c);
			}
			return true;
		}
	};

	std::unique_ptr<Backend> backendFor(Conf& c)
	{
		if (c.backend == "romm") return newRomm(c);
		return std::unique_ptr<Backend>(new Romhost(c));
	}

	// ---- the ROMs: the card made to match the server's list (PROTOCOL.md "What a device sync must do") ----------------
	struct RomResult { int downloaded = 0, removed = 0; std::vector<std::string> conflicts, errors; bool changed = false; };
	// Choosing games (owner, 0.5.0): settings homesync_games = all | chosen; the chosen ones in banana/homesync-games.txt,
	// a line each: the game's key, its first ROM file's roms-relative path ("gba/Name.gba"), as `banana-sync games` lists
	std::string gameKey(const MGame& g)
	{
		for (auto& f : g.files) if (!f.media) return f.path;
		return g.files.empty() ? std::string() : g.files[0].path;
	}
	std::string choiceFile() { return ROOT + "/banana/homesync-games.txt"; }
	std::set<std::string> readChoice()
	{
		std::set<std::string> out;
		std::ifstream f(choiceFile());
		std::string l;
		while (std::getline(f, l)) { while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back(); if (!l.empty() && l[0] != '#') out.insert(l); }
		return out;
	}

	// chosen: null = every game; else only those games come to the card, and the others' files already on it stay (an
	// unticked game isn't removed: un-choosing never deletes, as a skipped system's games stay)
	// toSd2 (settings homesync_roms_to = sd2): new games go to the second SD card instead of this one. A game the sync
	// already put on a card stays on that card (its updates and its removal happen there); a new game whose files are
	// on either card already (copied by hand) is adopted where it is. With the second card out, the files that belong
	// on it wait for the next sync (an error says so): they never go to this card instead
	RomResult syncRoms(Backend& b, Conf& c, const Manifest& m, const std::set<std::string>& skipFolders, const std::set<std::string>* chosen = nullptr,
		bool toSd2 = false)
	{
		RomResult res;
		std::map<std::string, const MFile*> want;
		std::set<std::string> keep;
		std::string sd2 = sd2Roms();
		std::set<std::string> onSd2;	// the wanted files that belong on the second SD card
		for (auto& s : m.systems) for (auto& g : s.games)
		{
			bool on = !chosen || chosen->count(gameKey(g));
			for (auto& f : g.files) { if (on) want[f.path] = &f; else keep.insert(f.path); }
			if (!on)
			{
				continue;
			}
			int known = -1;		// the card a file of the game was synced to and is still on: 0 this one, 1 the second
			for (auto& f : g.files)
			{
				if (c.files.count(f.path))
				{
					// (a file deleted from its card since doesn't hold the game there: it comes back as a new game,
					// to the card Settings says; one on the second card while that's out is taken as still there)
					bool sdk = c.where.count(f.path) > 0;
					std::string root = sdk ? sd2 : ROMS;
					if (root.empty() || exists(root + "/" + f.path))
					{
						known = sdk ? 1 : 0;
						break;
					}
				}
			}
			bool sd = known >= 0 ? known == 1 : toSd2;
			if (known < 0)
			{
				bool here = true, there = !sd2.empty(), any = false;
				for (auto& f : g.files)
				{
					if (!f.media)
					{
						any = true;
						here = here && exists(ROMS + "/" + f.path);
						there = there && exists(sd2 + "/" + f.path);
					}
				}
				if (any && here)
				{
					sd = false;
				}
				else if (any && there)
				{
					sd = true;
				}
			}
			if (sd)
			{
				for (auto& f : g.files)
				{
					onSd2.insert(f.path);
				}
			}
		}
		int waiting = 0;	// files on (or for) the second SD card while it's out
		std::vector<const MFile*> todo;
		for (auto& kv : want)
		{
			const MFile& f = *kv.second;
			bool sd = onSd2.count(f.path) > 0;
			if (sd && sd2.empty())
			{
				waiting++;
				continue;
			}
			std::string dst = (sd ? sd2 : ROMS) + "/" + f.path;
			auto st = c.files.find(f.path);
			if (st != c.files.end() && (c.where.count(f.path) > 0) != sd)
			{
				// synced to the other card once but gone from it (the game was placed again above): not ours here, so a
				// file at this path is judged as one copied by hand
				c.files.erase(st);
				c.where.erase(f.path);
				st = c.files.end();
			}
			bool there = exists(dst);
			if (st != c.files.end() && st->second == f.key() && there && (f.size < 0 || f.media || fileSize(dst) == f.size)) continue;
			if (there && st == c.files.end())
			{
				if (f.media) continue;	// (the user's own art, banana-scrape's: kept, not ours)
				std::string lk = localKey(f, dst);
				bool same = !lk.empty() ? lk == f.key() : (f.size >= 0 && fileSize(dst) == f.size);
				if (same)
				{
					// the same file, copied by hand: adopted, on the card it's on
					c.files[f.path] = f.key();
					if (sd)
					{
						c.where[f.path] = "sd2";
					}
					continue;
				}
				res.conflicts.push_back(f.path);	// the user's own file at that path: never overwritten
				continue;
			}
			if (there && st != c.files.end() && !f.media && !unchangedOnCard(st->second, dst))
			{
				res.conflicts.push_back(f.path);	// ours, but changed on the card since (a patched ROM): left alone
				continue;
			}
			todo.push_back(&f);
		}
		std::vector<std::string> gone;
		bool emptyList = want.empty() && !c.files.empty();
		for (auto& kv : c.files)
		{
			if (want.count(kv.first)) continue;
			std::string folder = kv.first.substr(0, kv.first.find('/'));
			if (skipFolders.count(folder)) continue;	// (a system skipped on this handheld: its games stay as they are)
			if (keep.count(kv.first)) continue;			// (a game not chosen: left as it is on the card)
			gone.push_back(kv.first);
		}
		if (emptyList) gone.clear();	// a server listing nothing at all while the card has its games: nothing removed
		long long need = 0, needSd2 = 0;
		for (auto* f : todo)
		{
			need += std::max(0LL, f->size);
			if (onSd2.count(f->path))
			{
				needSd2 += std::max(0LL, f->size);
			}
		}
		long long fr = freeBytes(ROMS), frSd2 = sd2.empty() ? -1 : freeBytes(sd2);
		say("roms: %zu to download (%.1f MB), %zu to remove, %zu conflicts, %.1f GB free%s%s", todo.size(), need / 1e6, gone.size(), res.conflicts.size(), fr / 1e9,
			m.skipped ? (", " + std::to_string(m.skipped) + " games skipped").c_str() : "",
			frSd2 >= 0 ? (", " + std::to_string(needSd2 / 1000000) + " MB of it to the second SD card (" + std::to_string(frSd2 / 1000000000) + " GB free)").c_str() : "");
		if (fr >= 0 && need - needSd2 > fr - (64LL << 20)) { res.errors.push_back("not enough space on the card for this sync"); return res; }
		if (frSd2 >= 0 && needSd2 > frSd2 - (64LL << 20))
		{
			res.errors.push_back("not enough space on the second SD card for this sync");
			return res;
		}
		if (dryRun)
		{
			for (auto* f : todo) say("  + %s", f->path.c_str());
			for (auto& g : gone) say("  - %s", g.c_str());
			for (auto& g : res.conflicts) say("  ! (yours, left alone) %s", g.c_str());
			return res;
		}
		long long doneBytes = 0;
		int i = 0;
		for (auto* f : todo)
		{
			i++;
			bool sd = onSd2.count(f->path) > 0;
			std::string dst = (sd ? sd2 : ROMS) + "/" + f->path, err;
			std::string label = baseOf(f->path);
			auto show = [&](long long now) {
				long long mb = (doneBytes + now) >> 20, tot = std::max(1LL, need >> 20);
				status("running " + std::to_string(std::min(mb, tot)) + " " + std::to_string(tot) + " Games " + std::to_string(i) + "/" + std::to_string(todo.size()) + "|" + label);
			};
			show(0);
			progress = [&](long long now, long long) { show(now); };
			bool ok;
			if (f->generated) ok = writeAtomic(dst, f->content);
			else
			{
				std::string algo = !f->sha1.empty() ? "sha1" : !f->md5.empty() ? "md5" : "";
				ok = fetchTo(f->url, b.auth(), dst, f->media ? -1 : f->size, algo, !f->sha1.empty() ? f->sha1 : f->md5, err);
			}
			progress = nullptr;
			doneBytes += std::max(0LL, f->size);
			if (!ok) { res.errors.push_back(f->path + ": " + (err.empty() ? "write failed" : err)); continue; }
			c.files[f->path] = f->key();
			if (sd)
			{
				c.where[f->path] = "sd2";
			}
			else
			{
				c.where.erase(f->path);
			}
			res.downloaded++;
			res.changed = true;
			saveConf(c);	// progress kept: a sync cut short goes on from here
			say("  [%d/%zu] %s", i, todo.size(), f->path.c_str());
		}
		for (auto& rel : gone)
		{
			bool sd = c.where.count(rel) > 0;
			if (sd && sd2.empty())
			{
				waiting++;	// (on the second SD card, which is out: removed when it's back)
				continue;
			}
			std::string root = sd ? sd2 : ROMS, p = root + "/" + rel;
			if (exists(p) && fileSize(p) > 0 && !unchangedOnCard(c.files[rel], p)) res.errors.push_back(rel + ": changed on the card, kept");
			else if (unlink(p.c_str()) == 0 || errno == ENOENT) { pruneDirs(dirOf(p), root); res.removed++; res.changed = true; }
			c.files.erase(rel);
			c.where.erase(rel);
		}
		// gamelist.xml per system and card: the entries of the games whose files are all in place, in the gamelist of
		// the card the game is on (the second card's while it's in)
		for (auto& s : m.systems)
		{
			std::vector<std::vector<std::pair<std::string, std::string>>> ours[2];
			for (auto& g : s.games)
			{
				bool all = !g.gl.empty();
				for (auto& f : g.files) { auto st = c.files.find(f.path); if (!f.media && (st == c.files.end() || st->second != f.key())) all = false; }
				if (!all) continue;
				int k = !g.files.empty() && c.where.count(g.files[0].path) ? 1 : 0;
				if (k == 1 && sd2.empty())
				{
					continue;
				}
				// the picture only when it is there
				std::string root = k == 1 ? sd2 : ROMS;
				auto gl = g.gl;
				for (auto it = gl.begin(); it != gl.end();)
				{
					if ((it->first == "image" || it->first == "thumbnail") && !exists(root + "/" + s.folder + "/" + normPath(it->second))) it = gl.erase(it);
					else ++it;
				}
				ours[k].push_back(gl);
			}
			for (int k = 0; k < 2; k++)
			{
				std::string root = k == 1 ? sd2 : ROMS, key = (k == 1 ? "@sd2/" : "") + s.folder;
				if (root.empty())
				{
					continue;	// (the second SD card is out: its gamelist when it's back)
				}
				std::string path = root + "/" + s.folder + "/gamelist.xml", out;
				auto& before = c.gamelist[key];
				std::set<std::string> drop(before.begin(), before.end());
				bool had = exists(path);
				if (ours[k].empty() && drop.empty()) { c.gamelist.erase(key); continue; }
				if (!mergeGamelist(had ? readFile(path) : "", drop, ours[k], out)) { res.errors.push_back(s.folder + "/gamelist.xml can't be read: left alone"); continue; }
				if ((!had || readFile(path) != out) && !writeAtomic(path, out)) { res.errors.push_back(s.folder + "/gamelist.xml: write failed"); continue; }
				before.clear();
				for (auto& e : ours[k]) for (auto& kv : e) if (kv.first == "path") before.push_back(kv.second);
				res.changed = true;
			}
		}
		std::set<std::string> listed;
		for (auto& s : m.systems) listed.insert(s.folder);
		for (auto it = c.gamelist.begin(); it != c.gamelist.end();)
		{
			bool sd = it->first.compare(0, 5, "@sd2/") == 0;
			std::string folder = sd ? it->first.substr(5) : it->first, root = sd ? sd2 : ROMS;
			if (listed.count(folder) || skipFolders.count(folder) || emptyList || root.empty()) { ++it; continue; }
			std::string path = root + "/" + folder + "/gamelist.xml", out;
			std::set<std::string> drop(it->second.begin(), it->second.end());
			if (exists(path) && mergeGamelist(readFile(path), drop, {}, out)) writeAtomic(path, out);
			it = c.gamelist.erase(it);
		}
		if (waiting)
		{
			// (an error, so the server's list isn't taken as done: the next sync looks at these files again)
			res.errors.push_back("the second SD card is out, has no roms folder or its games are off in Settings: " + std::to_string(waiting) +
				(waiting == 1 ? " file waits" : " files wait") + " for it");
		}
		if (res.errors.empty()) c.etag = m.etag;
		saveConf(c);
		b.report(m, res.downloaded, res.removed, res.conflicts, res.errors);
		return res;
	}

	// ---- discovery and pairing ---------------------------------------------------------------------------------------
	std::string normUrl(std::string a, int defPort)
	{
		a = trim(a);
		while (!a.empty() && a.back() == '/') a.pop_back();
		if (a.find("://") == std::string::npos) a = "http://" + a;
		size_t host = a.find("://") + 3;
		if (a.find(':', host) == std::string::npos && a.find('/', host) == std::string::npos && defPort) a += ":" + std::to_string(defPort);
		return a;
	}
	std::vector<std::pair<std::string, std::string>> discover(double wait)
	{
		std::vector<std::pair<std::string, std::string>> out;
		int s = socket(AF_INET, SOCK_DGRAM, 0);
		if (s < 0) return out;
		int one = 1;
		setsockopt(s, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
		timeval tv = { 0, 300000 };
		setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
		sockaddr_in a = {};
		a.sin_family = AF_INET;
		const char* port = getenv("BANANA_SYNC_DISCOVER_PORT");
		a.sin_port = htons(port ? atoi(port) : 8092);
		const char* to = getenv("BANANA_SYNC_DISCOVER_ADDR");	// (tests: a unicast address)
		a.sin_addr.s_addr = to ? inet_addr(to) : INADDR_BROADCAST;
		std::set<std::string> seen;
		for (int tries = 0; tries < 2; tries++)
		{
			sendto(s, "ROMHOST?", 8, 0, (sockaddr*)&a, sizeof a);
			timeval t0; gettimeofday(&t0, nullptr);
			while (true)
			{
				timeval t1; gettimeofday(&t1, nullptr);
				if ((t1.tv_sec - t0.tv_sec) + (t1.tv_usec - t0.tv_usec) / 1e6 > wait / 2) break;
				char buf[4096];
				sockaddr_in from = {}; socklen_t fl = sizeof from;
				ssize_t n = recvfrom(s, buf, sizeof buf - 1, 0, (sockaddr*)&from, &fl);
				if (n <= 0) continue;
				buf[n] = 0;
				JPtr j = banana::parseJson(buf);
				if (!j || j->str("service") != "romhost") continue;
				std::string url = "http://" + std::string(inet_ntoa(from.sin_addr)) + ":" + fmtNum(j->num("port", 8092));
				if (seen.insert(url).second) out.push_back({ url, j->str("name") });
			}
			if (!out.empty()) break;
		}
		close(s);
		return out;
	}
	// libcurl's codes for an HTTPS server not trusted (CURLE_PEER_FAILED_VERIFICATION) and homesync_ca's file not read
	// (CURLE_SSL_CACERT_BADFILE)
	enum { CURL_UNTRUSTED = 60, CURL_BAD_CA = 77 };
	// what answers at an address: Romhost's hello, RomM's heartbeat. tls: the HTTPS try's CURL_UNTRUSTED or CURL_BAD_CA
	std::string probe(const std::string& addr, std::string& url, std::string& name, int* tls = nullptr)
	{
		std::vector<std::string> tries;
		std::string a = trim(addr);
		bool hasPort = a.find(':', a.find("://") == std::string::npos ? 0 : a.find("://") + 3) != std::string::npos;
		bool scheme = a.find("://") != std::string::npos, https = lower(a.substr(0, 8)) == "https://";
		if (hasPort || scheme) tries.push_back(normUrl(a, 0));
		// a bare address: Romhost's port first (the LAN's usual server), then the web ports, and https (443) last (#109);
		// https typed without a port is that server's 443 alone, and an address with a port is taken as typed
		if (!hasPort && !https) { tries.push_back(normUrl(a, 8092)); tries.push_back(normUrl(a, 8080)); tries.push_back(normUrl(a, 80)); }
		if (!hasPort && !scheme) tries.push_back(normUrl("https://" + a, 0));
		for (auto& u : tries)
		{
			Req q; q.url = u + "/api/v1/hello"; q.timeout = 5;
			Resp r = http(q);
			if (tls && (r.curl == CURL_UNTRUSTED || r.curl == CURL_BAD_CA)) *tls = r.curl;
			if (r.code == 0) continue;	// (nothing there, on this scheme and port: the heartbeat wouldn't get further)
			JPtr j = r.code == 200 ? r.json() : nullptr;
			if (j && j->str("service") == "romhost") { url = u; name = j->str("name"); return "romhost"; }
			q.url = u + "/api/heartbeat";
			r = http(q);
			j = r.code == 200 ? r.json() : nullptr;
			if (j && j->get("SYSTEM")) { url = u; name = "RomM " + j->get("SYSTEM")->str("VERSION"); return "romm"; }
		}
		return "none";
	}
	std::string deviceName()
	{
		std::string model = trim(readFile("/proc/device-tree/model"));
		model.erase(std::find(model.begin(), model.end(), '\0'), model.end());
		if (model.find("Plus") != std::string::npos) return "RG DS Plus";
		if (model.find("RG") != std::string::npos || model.find("DS") != std::string::npos) return "RG DS";
		char h[64] = {}; gethostname(h, sizeof h - 1);
		return h[0] ? h : "BananaOS";
	}
	int pair(const std::string& addr, const std::string& codeIn)
	{
		std::string url, name;
		int tls = 0;
		std::string kind = probe(addr, url, name, &tls);
		if (kind == "none" && tls == CURL_BAD_CA)
		{
			status("error homesync_ca can't be read");
			say("homesync_ca: %s can't be read", CA.c_str());
			return 1;
		}
		if (kind == "none" && tls == CURL_UNTRUSTED)
		{
			status("error the server's certificate isn't trusted (homesync_ca)");
			say("the HTTPS server at %s isn't trusted: its CA's certificate as homesync_ca, or homesync_tls = off (not "
				"recommended: any certificate is then taken), in banana/settings.conf", addr.c_str());
			return 1;
		}
		if (kind == "none") { status("error no server at " + addr); say("no Romhost or RomM answers at %s", addr.c_str()); return 1; }
		if (!tlsVerify && url.compare(0, 8, "https://") == 0) say("certificate checks are off (homesync_tls)");
		// (RomM: the card's id, its device's hostname and identifier, before anything is asked: without it every such card
		// would be one device)
		if (kind == "romm" && cardId().empty()) { status("error can't write banana/sync-id"); say("can't write %s/banana/sync-id", ROOT.c_str()); return 1; }
		Conf c = loadConf();
		std::string code = trim(codeIn), dev = getenv("BANANA_SYNC_NAME") ? getenv("BANANA_SYNC_NAME") : deviceName();
		Conf n;
		n.backend = kind; n.server = url; n.serverName = name; n.name = dev;
		if (kind == "romm" && name.compare(0, 5, "RomM ") == 0) n.serverVersion = name.substr(5);
		// a new server: a new state (another server's files and versions mean nothing here); the files stay on the card.
		// The same server again: its state as it is, the saves' part too (what was agreed, held or waiting still holds)
		if (c.server == url && c.backend == kind) { n.files = c.files; n.gamelist = c.gamelist; takeSaves(n, c); }
		if (kind == "romhost")
		{
			if (code.empty()) { status("error the pairing code from Homeport is needed"); return 1; }
			std::string b = "{\"code\": " + jsonStr(code) + ", \"name\": " + jsonStr(dev) + "}";
			Req q; q.method = "POST"; q.url = url + "/api/v1/pair"; q.body = &b; q.headers = { "Content-Type: application/json" };
			Resp r = http(q);
			JPtr j = r.code == 200 ? r.json() : nullptr;
			if (!j || j->str("token").empty()) { status("error " + r.error()); say("pairing failed: %s", r.error().c_str()); return 1; }
			n.token = j->str("token"); n.device = fmtNum(j->num("device")); n.deviceNum = true; n.name = j->str("deviceName", dev);
			if (const JVal* s = j->get("server")) n.serverName = s->str("name", name);
		}
		else if (code.compare(0, 4, "rmm_") == 0) n.token = code;	// a client token pasted
		else if (!code.empty())
		{
			// a client token's pairing code (RomM: Profile > Client tokens > Pair)
			std::string b = "{\"code\": " + jsonStr(code) + "}";
			Req q; q.method = "POST"; q.url = url + "/api/client-tokens/exchange"; q.body = &b; q.headers = { "Content-Type: application/json" };
			Resp r = http(q);
			JPtr j = r.code == 200 ? r.json() : nullptr;
			if (!j || j->str("raw_token").empty()) { status("error " + r.error()); say("pairing failed: %s", r.error().c_str()); return 1; }
			n.token = j->str("raw_token"); n.device = j->str("device_id");
		}
		else
		{
			// RomM's device pairing: a code shown here, typed (approved) in RomM's web UI at /pair/device
			// (as the device it then registers: RomM makes the device from this when the code is approved)
			std::string b = "{\"client_device_identifier\": " + jsonStr("bananaos-" + cardId()) + ", " + deviceFields(n) + ", "
				"\"requested_scopes\": [\"me.read\", \"roms.read\", \"platforms.read\", \"assets.read\", \"assets.write\", \"devices.read\", \"devices.write\"]}";
			Req q; q.method = "POST"; q.url = url + "/api/auth/device/init"; q.body = &b; q.headers = { "Content-Type: application/json" };
			Resp r = http(q);
			JPtr j = (r.code == 200 || r.code == 201) ? r.json() : nullptr;
			if (!j || j->str("device_code").empty()) { status("error " + (r.code == 404 ? std::string("this RomM has no device pairing (update it, or type a client token's code)") : r.error())); return 1; }
			std::string dc = j->str("device_code"), uc = j->str("user_code");
			int interval = std::max(2, (int)j->num("interval", 5)), left = (int)j->num("expires_in", 600);
			// the page's address with the code in it (verification_path_complete): RomM's /pair/device has no field to
			// type a code into, it takes it from ?user_code= only (#141)
			std::string verify = url + j->str("verification_path_complete", j->str("verification_path", "/pair/device") + "?user_code=" + urlEsc(uc));
			status("pair " + uc + " " + verify);
			say("On RomM, open %s and enter %s", verify.c_str(), uc.c_str());
			std::string body = "{\"device_code\": " + jsonStr(dc) + "}";
			for (; left > 0; left -= interval)
			{
				sleep(interval);
				Req t; t.method = "POST"; t.url = url + "/api/auth/device/token"; t.body = &body; t.headers = { "Content-Type: application/json" };
				Resp tr = http(t);
				JPtr tj = tr.code == 200 ? tr.json() : nullptr;
				if (tj && !tj->str("access_token").empty()) { n.token = tj->str("access_token"); n.device = tj->str("device_id"); break; }
				std::string why = tr.error();
				if (why == "slow_down") interval += 5;
				else if (why != "authorization_pending" && tr.code != 0) { status("error " + (why == "access_denied" ? std::string("refused in RomM") : why == "expired_token" ? std::string("the code ran out") : why)); return 1; }
			}
			if (n.token.empty()) { status("error the code ran out"); return 1; }
		}
		if (kind == "romm")
		{
			// the card as a RomM device, whichever way it paired; re-paired with the same server, the same device
			if (n.device.empty() && c.server == url && c.backend == kind) n.device = c.device;
			std::string e = registerDevice(n);
			if (!e.empty()) say("not registered as a device yet (%s): the next sync tries again", e.c_str());
		}
		if (!saveConf(n)) { status("error can't write " + CONF); return 1; }
		status("paired " + (n.serverName.empty() ? url : n.serverName));
		say("paired with %s (%s) as \"%s\"", n.serverName.c_str(), url.c_str(), n.name.c_str());
		return 0;
	}

	// the awake lock (bananad doesn't sleep for idleness while it names a live process): taken only when free
	struct Awake
	{
		bool mine = false;
		void take()
		{
			std::string f = RUN + "/awake.pid";
			int pid = 0;
			std::ifstream(f) >> pid;
			if (pid > 0 && pid != getpid() && kill(pid, 0) == 0) return;	// (banana-scrape holds it: it keeps the handheld up for both)
			if (FILE* o = fopen(f.c_str(), "w")) { fprintf(o, "%d\n", (int)getpid()); fclose(o); mine = true; }
		}
		~Awake()
		{
			if (!mine) return;
			int pid = 0;
			std::ifstream(RUN + "/awake.pid") >> pid;
			if (pid == getpid()) unlink((RUN + "/awake.pid").c_str());
		}
	};

	int doSync(bool savesOnly, const std::string& autoWhy, const std::string& onlyRom)
	{
		auto set = readConf(SETTINGS);
		auto get = [&](const std::string& k, const std::string& d) { auto i = set.find(k); return i == set.end() || i->second.empty() ? d : i->second; };
		bool queued = false;
		Conf c = loadConf();
		if (!c.paired()) { if (autoWhy.empty()) status("error not paired"); return 2; }
		if (!autoWhy.empty())
		{
			std::string a = get("homesync_auto", "off");
			if (a == "off") return 0;
			// (Wi-Fi back with only "Leaving a game": the saves that couldn't be sent then, sent now)
			if (autoWhy == "wifi" && a != "both")
			{
				if (c.queue.empty() || c.backend != "romm") return 0;
				savesOnly = true;
				queued = true;
			}
			if (autoWhy == "quit") savesOnly = true;	// (leaving a game: only the saves, it's quick)
			// (before a game starts: RomM's slots only, one game's, under the card's ROMs folder; Romhost's plan is all the
			// saves at once)
			if (autoWhy == "launch" && (onlyRom.empty() || onlyRom[0] == '/' || c.backend != "romm" || get("homesync_launch", "on") != "on")) return 0;
		}
		if (!tlsVerify && c.server.compare(0, 8, "https://") == 0) say("certificate checks are off (homesync_tls)");
		bool states = get("homesync_states", "off") == "on";
		std::set<std::string> skip;
		for (auto& kv : set) if (kv.first.compare(0, 9, "homesync.") == 0 && kv.second == "off") skip.insert(kv.first.substr(9));
		bool pick = get("homesync_games", "all") == "chosen";
		std::set<std::string> chosen;
		if (pick) { chosen = readChoice(); c.etag.clear(); }	// (the list in full: the choice may have changed since)
		auto b = backendFor(c);
		b->skip = skip;
		b->chosen = pick ? &chosen : nullptr;
		b->ownCovers = ownCoversOf(SYSTEMS);
		b->onlyRom = onlyRom;
		b->onlyQueued = queued;
		b->beforeGame = autoWhy == "launch";
		if (!onlyRom.empty() && c.backend != "romm") say("Romhost decides which saves move: all of them synced, not only %s's", onlyRom.c_str());
		// (one game's: its states go too, when they are on, except while it waits to start)
		if (!onlyRom.empty()) { savesOnly = true; if (autoWhy == "launch") states = false; }
		bool roms = !savesOnly && get("homesync_roms", "on") == "on";
		Awake awake;
		if (roms) awake.take();
		// (the line before the server's first answer: the menu's wait before a game gives that answer 5 s from it)
		const std::string first = "running 0 0 |" + (c.serverName.empty() ? c.server : c.serverName);
		status(first);
		std::vector<std::string> errors, conflicts;
		int downloaded = 0, removed = 0, up = 0, down = 0, sconf = 0;
		std::string note;
		if (roms)
		{
			Manifest m;
			std::string err;
			if (!b->manifest(m, skip, err))
			{
				if (err == "unpaired") { status("error unpaired"); c.last = "unpaired"; saveConf(c); return 1; }
				errors.push_back(err);
			}
			else if (m.unchanged) say("roms: up to date");
			else
			{
				// (the second SD card only on a handheld with a second slot, as the sd2 script finds: elsewhere the
				// setting, say from a card moved over from an RG DS Plus, is this card)
				bool toSd2 = get("homesync_roms_to", "card") == "sd2" && exists(RUN + "/sd2/slot");
				if (toSd2 && sd2Roms().empty() && get("sd2_roms", "on") != "off")
				{
					sd2MakeRoms();	// (a fresh card; not one whose games are off in Settings)
				}
				RomResult r = syncRoms(*b, c, m, skip, pick ? &chosen : nullptr, toSd2);
				downloaded = r.downloaded; removed = r.removed; conflicts = r.conflicts;
				for (auto& e : r.errors) errors.push_back(e);
			}
		}
		// (the saves one run at a time: a sync of one game's beside a sync copying games waits for the other's to end,
		// and takes the saves' state as that one left it)
		// (and asked again once it has them: a game can start during that wait, with no lock of its own if a sync held it then)
		const char* running = "saves: a game is running, next time";
		// (a wait for the lock said before a game, when there is one: the menu waits it out, up to its cap, instead of
		// starting the game at 5 s under the sync after the last one, still placing. The line before the first answer
		// is put back once the lock is had, so a server that then doesn't answer is given its 5 s from there, not the
		// wait's length over)
		auto lock = [&]()
		{
			if (lockSaves(0)) return true;
			if (b->beforeGame) status("waiting 0 0 |saves");
			bool had = lockSaves(onlyRom.empty() ? 60 : 10);
			if (had && b->beforeGame) status(first);
			return had;
		};
		if (gameRunning()) errors.push_back(running);
		else if (!lock()) errors.push_back("saves: another sync is saving, next time");
		else if (gameRunning()) errors.push_back(running);
		else
		{
			takeSaves(c, loadConf());
			holdSaves = true;
			// (a run cut short last time: how far each operation got, before anything else touches the saves; a save
			// found in place is agreed in the record, as the run would have done next)
			if (!dryRun)
			{
				std::string rec = journalRecover(&c);
				for (size_t a = 0, e; (e = rec.find('\n', a)) != std::string::npos; a = e + 1) say("  journal: %s", rec.substr(a, e - a).c_str());
				savesFlatFirst();
			}
			b->saves(states, up, down, sconf, errors, note);
		}
		for (auto& e : errors) say("  error: %s", e.c_str());
		if (!note.empty()) say("%s", note.c_str());
		char sum[256];
		snprintf(sum, sizeof sum, "%d %d %d %d %d", downloaded, removed, up, down, sconf + (int)conflicts.size());
		c.last = std::string(errors.empty() ? "ok " : "errors ") + sum + (errors.empty() ? "" : "|" + errors[0]);
		c.lastTime = (double)time(nullptr);
		saveConf(c);
		status("done " + std::string(sum) + " " + std::to_string(errors.size()) + "|" + (errors.empty() ? note : errors[0]));
		say("done: %d games down, %d removed, %d saves up, %d down, %d conflicts, %zu errors", downloaded, removed, up, down, sconf + (int)conflicts.size(), errors.size());
		return errors.empty() ? 0 : 1;
	}
}

int main(int argc, char** argv)
{
	std::vector<std::string> args;
	std::string autoWhy;
	bool savesOnly = false;
	std::string onlyRom;
	for (int i = 1; i < argc; i++)
	{
		std::string a = argv[i];
		auto val = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
		if (a == "--root") ROOT = val();
		else if (a == "--roms") ROMS = val();
		else if (a == "--conf") CONF = val();
		else if (a == "--settings") SETTINGS = val();
		else if (a == "--systems") SYSTEMS = val();
		else if (a == "--status") STATUS = val();
		else if (a == "--run") RUN = val();
		else if (a == "--dry-run") dryRun = true;
		else if (a == "--saves-only") savesOnly = true;
		else if (a == "--auto") autoWhy = val();
		else if (a == "--rom") onlyRom = val();
		else if (a == "-v") verbose = true;
		else args.push_back(a);
	}
	if (ROMS.empty()) ROMS = ROOT + "/roms";
	if (CONF.empty()) CONF = ROOT + "/banana/romsync.json";
	if (SETTINGS.empty()) SETTINGS = ROOT + "/banana/settings.conf";
	if (SYSTEMS.empty())
	{
		// the menu's merged list (the card's banana/systems.conf over the image's), else the image's own
		const char* e = getenv("BANANA_SYSTEMS");
		SYSTEMS = e ? e : ROOT == "/data" ? (access("/run/banana/systems.conf", R_OK) == 0 ? "/run/banana/systems.conf" :
			"/etc/banana/systems.conf") : "";
	}
	if (!onlyRom.empty()) onlyRom = romsRelative(onlyRom);
	// (before a game starts, while it waits: a server that doesn't answer soon is left for the sync after the game)
	if (autoWhy == "launch") { connectTimeout = 3; callTimeout = 10; }
	// (and its own status file: the menu waits on it, and a run stopped so a game can start says nothing on the Sync row)
	if (autoWhy == "launch") STATUS = RUN + "/launch.status";
	// an https server's certificate: signed by the owner's own CA (its certificate file, data-relative or absolute), or,
	// the owner's choice for one of its own making, not checked at all (homesync_tls = off)
	{
		auto set = readConf(SETTINGS);
		CA = set["homesync_ca"];
		if (!CA.empty() && CA[0] != '/') CA = ROOT + "/" + CA;
		tlsVerify = set["homesync_tls"] != "off";
	}
	if (args.empty()) { fprintf(stderr, "usage: banana-sync discover | probe <address> | pair <address> [code] | forget | sync [--saves-only] [--rom <path>] [--auto quit|wifi|launch] [--dry-run] | status | games | blank <file> [<folder>]\n"); return 2; }
	std::string cmd = args[0];
	// (exit 0: blank, a core's fresh save holding nothing yet; what run-retroarch asks before it seeds the GBA folder)
	if (cmd == "blank" && args.size() > 1)
	{
		bool ok;
		std::string b = readFile(args[1], &ok);
		return !ok ? 2 : blankSave(b, args.size() > 2 ? args[2] : "") ? 0 : 1;
	}
	if (!cu.load()) { status("error libcurl is missing"); fprintf(stderr, "banana-sync: libcurl is missing\n"); return 1; }
	if (cmd == "discover")
	{
		for (auto& s : discover(2.0)) printf("ROMHOST\t%s\t%s\n", s.first.c_str(), s.second.c_str());
		return 0;
	}
	if (cmd == "probe" && args.size() > 1)
	{
		std::string url, name;
		std::string k = probe(args[1], url, name);
		printf("%s %s %s\n", k.c_str(), url.c_str(), name.c_str());
		return k == "none";
	}
	if (cmd == "status")
	{
		Conf c = loadConf();
		if (!c.paired()) { printf("not paired\n"); return 0; }
		printf("paired with %s (%s, %s) as %s; %zu files, %zu saves synced; last: %s\n", c.serverName.c_str(), c.server.c_str(), c.backend.c_str(), c.name.c_str(),
			c.files.size(), c.saves.size() + c.states.size(), c.last.c_str());
		return 0;
	}
	// banana-sync games: the server's games for the menu's chooser, a line each: system folder TAB key TAB title TAB
	// bytes TAB chosen (1/0) TAB on the card (1/0); "error <why>" on failure
	if (cmd == "games")
	{
		Conf c = loadConf();
		if (!c.paired()) { printf("error not paired\n"); return 2; }
		auto b = backendFor(c);
		c.etag.clear();
		b->c.etag.clear();
		Manifest m;
		std::string err;
		if (!b->manifest(m, {}, err)) { printf("error %s\n", err.c_str()); return 1; }
		std::set<std::string> chosen = readChoice();
		std::string sd2 = sd2Roms();	// (a game on either card is on the handheld)
		for (auto& s : m.systems)
			for (auto& g : s.games)
			{
				std::string k = gameKey(g);
				if (k.empty()) continue;
				long long bytes = 0; bool there = true;
				for (auto& f : g.files)
				{
					if (!f.media)
					{
						bytes += std::max(0LL, f.size);
						if (!exists(ROMS + "/" + f.path) && (sd2.empty() || !exists(sd2 + "/" + f.path)))
						{
							there = false;
						}
					}
				}
				std::string t = g.title.empty() ? baseOf(k) : g.title;
				for (char& ch : t) if (ch == '\t' || ch == '\n') ch = ' ';
				printf("%s\t%s\t%s\t%lld\t%d\t%d\n", s.folder.c_str(), k.c_str(), t.c_str(), bytes, chosen.count(k) ? 1 : 0, there ? 1 : 0);
			}
		return 0;
	}
	if (cmd == "forget")
	{
		// (under the state file's lock: a run writing its part back meanwhile would write the pairing back with it)
		int lk = open((RUN + "/romsync.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
		if (lk >= 0) while (flock(lk, LOCK_EX) != 0 && errno == EINTR) {}
		int rc = unlink(CONF.c_str()) != 0 && errno != ENOENT ? 1 : 0;
		if (lk >= 0) close(lk);
		if (rc) return rc;
		status("");
		return 0;
	}
	// one at a time (the menu's Sync now beside an automatic one); a sync of one game's save beside it, by save.lock
	if (cmd == "sync" && !onlyRom.empty())
	{
		// (no sync.pid and not sync.status: the menu's Home server page and its Stop are for a sync copying games, which
		// holds sync.lock, and this run doesn't. A status line of its own, as the wait before a game has launch.status:
		// on sync.status the page would offer a Stop with nothing to stop, and take a line left unchanged through a slow
		// upload for a sync gone)
		holdRoms = holdSaves = false;
		if (autoWhy != "launch") STATUS = RUN + "/gameend.status";
		int other = open((RUN + "/sync.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
		bool beside = other >= 0 && flock(other, LOCK_EX | LOCK_NB) != 0;
		if (other >= 0) close(other);
		// (the report on the card for the sync after a game, as for every sync: not beside a sync copying games, whose
		// report it would push aside mid-run, nor for the wait before a game, which reports on launch.status)
		logWanted = !dryRun && autoWhy != "launch" && !beside;
		catchKill(false);
		return doSync(true, autoWhy, onlyRom);
	}
	int lk = open((RUN + "/sync.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
	if (lk >= 0 && flock(lk, LOCK_EX | LOCK_NB) != 0) { if (autoWhy.empty()) fprintf(stderr, "banana-sync: already running\n"); return 3; }
	if (cmd == "pair" && args.size() > 1) return pair(args[1], args.size() > 2 ? args[2] : "");
	if (cmd == "sync")
	{
		// the pid for the menu's Stop (Settings > Network > Home server > Sync now while it runs)
		catchKill();
		std::ofstream(RUN + "/sync.pid") << getpid() << "\n";
		holdSaves = false;
		logWanted = !dryRun;
		int rc = doSync(savesOnly, autoWhy, "");
		unlink((RUN + "/sync.pid").c_str());
		return rc;
	}
	fprintf(stderr, "banana-sync: unknown command %s\n", cmd.c_str());
	return 2;
}
