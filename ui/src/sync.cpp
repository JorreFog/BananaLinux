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
//                                              an address with no scheme is tried on Romhost's port, 8080 and 80, then
//                                              on HTTPS; one with a scheme or a port as typed
//   banana-sync probe --romm <url>             RomM at that base URL and nowhere else: "romm <url> RomM <version>" (and
//                                              " old" before 5.4), or "fail <why> [detail]", why one of dns, refused,
//                                              timeout, tls, clock (tls with the clock not set yet), ca, redirect
//                                              <location>, access <location> (Cloudflare Access's login), denied
//                                              <status>, http <status>, notromm, noanswer <curl's message>
//   banana-sync pair [--romm] <address> [code] Romhost: the code from Homeport. RomM: no code = the device flow (the
//                                              status shows the code and the page that approves it, and the seconds
//                                              the code lasts: "pair <code> <url> <seconds>"); a code =
//                                              a client token's pairing code; rmm_... = a token pasted. Whichever,
//                                              the card is then a RomM device: bananaos-<banana/sync-id>, kept.
//                                              --romm: the address is RomM's base URL, checked as probe --romm does;
//                                              what fails says why as "error <why> [detail]" with probe's words
//   banana-sync forget                         the pairing (and the sync's state) dropped; the files stay
//   banana-sync sync [--saves-only] [--rom <path>] [--auto quit|wifi|launch] [--dry-run]
//                                              --rom: that one game's save and, with homesync_states, its states (its
//                                              ROM's path, roms-relative or absolute, on either SD card), beside a sync
//                                              that is copying games: one at a time over the saves only, its status in
//                                              <run>/gameend.status (--report <file>: there instead, the menu's own
//                                              file for each game's end). Romhost and a RomM before 5.4 sync every save
//                                              instead;
//                                              --auto launch --rom: the one before the game starts (homesync_launch),
//                                              the save alone, each request given less time, its status in
//                                              <run>/launch.status; RomM 5.4 and newer only
//   banana-sync status                         the pairing, the last sync, what is held and the vault, for a person
//   banana-sync saves [--rom <path>]           what the Saves page lists, tab-separated; nothing asked of the server
//   banana-sync resolve <key> card|server [--rom <path>]
//                                              a held save settled: the card's kept (an ordinary run of the saves),
//                                              or the server's from the vault. Says "ok" or "error <why>" on its last
//                                              line, as restore does
//   banana-sync restore <file>                 a kept copy in the vault as the card's save; the next sync sends it
//   banana-sync blank <file> [<folder>]        exit 0 when the save is blank (a core's fresh one); folder: under saves/
// The card: --root /data (saves/, states/, drastic/), --roms <root>/roms, --conf <root>/banana/romsync.json,
// --settings <root>/banana/settings.conf (homesync_roms, homesync_roms_to, homesync_states, homesync_auto,
// homesync_launch, homesync.<system>, homesync_ca: a PEM file of the CA that signed an https server's certificate,
// homesync_tls = off: no certificate checked; homesync_slot = custom with homesync_slot_name: the RomM slot saves sync
// with instead of autosave).
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
#include "get_list.h"
#include "json.h"
#include "save_adapt.h"
#include "save_paths.h"
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
#include <sstream>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

using namespace bsync;
using banana::JVal;
using banana::JPtr;

bool Backend::keepPlayed(Played& g, std::vector<std::string>& errors)
{
	if (onlyRom.empty() || onlyRom[0] == '/' || beforeGame || onlyQueued || dryRun) return false;
	size_t sl = onlyRom.find('/');
	if (sl == std::string::npos) return false;
	// (the ROM's folder is its system's, which systems.conf may name otherwise than the system's id)
	std::string sys = systemOfFolder(onlyRom.substr(0, sl));
	if (sys.empty()) return false;
	SysSaves ss = systemSaves(sys);
	if (!ss.known()) return false;
	g.ndsHeld = ss.folder == "nds" && ndsSaves(romPath(onlyRom)) != 0;
	g.rel = ss.rel(stemOf(baseOf(onlyRom)));
	std::string p = ROOT + "/" + g.rel, e, kept;
	// (the two DS saves not brought together: what was played may be DraStic's, so it is kept too, beside BananaDS's,
	// whatever BananaDS's is; the Saves page cuts its footer off when it is put back)
	std::string dsv = ROOT + "/drastic/backup/" + stemOf(baseOf(onlyRom)) + ".dsv";
	if (g.ndsHeld && exists(dsv) && !vaultKeep(g.rel, dsv, kept, e)) errors.push_back(g.rel + ": DraStic's can't be kept in the vault: " + e);
	bool ok;
	std::string bytes = readFile(p, &ok);
	if (!ok || blankSave(bytes, saveFolder(g.rel))) return false;
	g.md5 = hashStr(bytes, "md5");
	if (!vaultKeep(g.rel, p, g.vault, e)) { errors.push_back(g.rel + ": can't be kept in the vault: " + e); return false; }
	return true;
}

namespace
{
	// the DS game of a save, for nds-saves, where no better path is known (a kept copy put back, Romhost's download):
	// nds-saves goes by the stem alone, so the folder and extension guessed here don't matter to it. Where the game
	// is known (RomM's records carry its path), that is passed instead
	std::string romOfSave(const std::string& rel) { return ROMS + "/nds/" + stemOf(baseOf(rel)) + ".nds"; }

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
		void keepUnsent(std::vector<std::string>& errors)
		{
			Played g;
			if (keepPlayed(g, errors)) say("  kept in the vault, the server out of reach: %s", g.rel.c_str());
		}
		// PROTOCOL.md "Saves": the plan, then each action; the server decides conflicts and keeps every version
		bool saves(bool, int& up, int& down, int& conflicts, std::vector<std::string>& errors, std::string& note) override
		{
			Resp r = call("GET", "/api/v1/saves/rules");
			if (r.code != 200) { errors.push_back("saves: " + (r.code == 401 ? std::string("unpaired") : r.error())); keepUnsent(errors); return false; }
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
			if (!plan || !plan->get("actions")) { errors.push_back("saves: " + r.error()); keepUnsent(errors); return false; }
			int n = 0, total = (int)plan->get("actions")->arr.size();
			// (the game about to start again: each of its files is named "<its stem>.", a state's too, "<stem>.state.auto")
			std::string resumeName = resumeRom.empty() ? "" : stemOf(baseOf(resumeRom)) + ".";
			for (auto& a : plan->get("actions")->arr)
			{
				if (stopForGame(errors)) break;
				std::string rel = a->str("path"), act = a->str("action");
				n++;
				if (!safeRel(rel) || saveKind(rel).empty()) continue;
				// (the DS's two saves apart: the game left alone until its next start brings the two together; still
				// in the plan's list, so the server doesn't take it as deleted)
				std::string ndsWhy = ndsDivergedWhy(rel);
				if (!ndsWhy.empty()) { say("  not synced, %s: %s", ndsWhyLog(ndsWhy), rel.c_str()); continue; }
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
					// (not sent: the card's save is the only copy of what was played until it is, so a copy goes to the vault)
					if (u.code != 200)
					{
						errors.push_back(rel + ": " + u.error());
						std::string kept, e;
						if (vaultKeep(rel, p, kept, e)) say("  not sent, kept in the vault: %s", rel.c_str());
						else errors.push_back(rel + ": can't be kept in the vault: " + e);
						continue;
					}
					vaultGood(rel, body);
					if (act == "upload") { JPtr res = u.json(); c.saves[rel] = { res ? res->num("version") : 0, sums[rel], "" }; up++; say("  up %s", rel.c_str()); }
					else { conflicts++; say("  conflict (kept here; Homeport decides) %s", rel.c_str()); }
				}
				else if (act == "conflict") conflicts++;
				else if (act == "download")
				{
					// (the game about to start again after Power off after sleeping: its save and its states, the resume state
					// among them, are left as they are here, every file named for the game)
					if (!resumeName.empty() && baseOf(rel).compare(0, resumeName.size(), resumeName) == 0) { say("  not synced, about to start again after Power off after sleeping, its resume state with the save here: %s", rel.c_str()); continue; }
					if (exists(p) && hashFile(p, "sha1") != sums[rel]) { errors.push_back(rel + ": changed while syncing, next time"); continue; }
					if (dryRun) { say("  download %s", rel.c_str()); continue; }
					// (what the save held when the plan was checked, by the MD5 installSave compares: a game played and quit
					// during a slow fetch leaves another, which the server's version mustn't go over as agreed)
					std::string before = exists(p) ? hashFile(p, "md5") : "";
					std::string err, dl = RUN + "/sync-dl." + std::to_string(getpid());
					OpenOp op;
					Install in = fetchTo(c.server + "/api/v1/saves/file/" + pathEsc(rel) + "?version=" + fmtNum(a->num("version")), auth(), dl, -1, "sha1", a->str("sha1"), err)
						? installSave(rel, dl, fmtNum(a->num("version")), err, &before, "", &op.id) : Install::Failed;
					if (in == Install::Done) op.md5 = hashFile(p, "md5");
					if (in == Install::Blank) { say("  blank on the server, not taken: %s", rel.c_str()); continue; }
					if (in == Install::Failed) { dropDownload(dl); errors.push_back(rel + ": " + err); continue; }
					// (DraStic's .dsv brought into step with a DS save just installed, before either emulator starts; not in
					// step after all: not recorded, so the next sync looks again)
					if (saveFolder(rel) == "nds" && ndsSaves(romOfSave(rel)) != 0) { errors.push_back(rel + ": DraStic's save isn't in step with it, decided next time"); continue; }
					// (a game started while it was placed: not recorded, so the next sync decides again from what it finds)
					if (gameRunning()) { errors.push_back(rel + ": a game started while it was placed, decided next time"); continue; }
					c.saves[rel] = { a->num("version"), a->str("sha1"), "" };
					down++;
					say("  down %s", rel.c_str());
					// (the journal's entry ended once the record is written: a run killed between the two is agreed by
					// the next start)
					saveConf(c);
					op.close();
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
	std::set<std::string> readChoice() { auto v = readListLines(ROOT + "/banana/homesync-games.txt"); return std::set<std::string>(v.begin(), v.end()); }
	// RomM's Get games: the games asked for and not on the card yet, banana/homesync-get.txt, a key a line (as games
	// lists them), in the order asked. Any sync of the games brings them, and a game leaves the list once its files are
	// in place (or a file of the owner's own is at its path); one whose download failed stays for the next sync. Once
	// on the card a game is the owner's like any other: nothing updates it or removes it. The list is changed under
	// get.lock, the menu adding (with get_list's code, which banana-sync get also runs) while a sync takes games off it
	std::string getFile() { return ROOT + "/banana/homesync-get.txt"; }
	std::string getFailedFile() { return ROOT + "/banana/homesync-get-failed.txt"; }
	bool editGet(const std::vector<std::string>& add, const std::set<std::string>& drop) { return editGetList(ROOT + "/banana", RUN, add, drop); }
	// why each game still on Get games' list didn't come the last time a sync tried it (banana/homesync-get-failed.txt,
	// "<key> TAB <why>", for the page's Failed): a game that came, or was taken off the list, has no line
	// (files: each game's paths under the ROMs folder, as the manifest has them, a disc set's every disc: an error is a
	// game's when it names one of them)
	void noteGetFailed(const std::set<std::string>& tried, const std::vector<std::string>& passErrors, const std::map<std::string, std::vector<std::string>>& files)
	{
		editGetFailed(ROOT + "/banana", RUN, [&](const std::set<std::string>& listed, std::map<std::string, std::string>& why)
		{
			for (auto& k : tried)
			{
				if (!listed.count(k)) { why.erase(k); continue; }
				// (its own files' error when there is one, else what stopped the whole pass: no space, the server's list.
				// Never another game's file's)
				auto about = [&](const std::string& e, const std::string& path) { return e.compare(0, path.size() + 2, path + ": ") == 0; };
				auto anyGame = [&](const std::string& e) { for (auto& g : files) for (auto& f : g.second) if (about(e, f)) return true; return about(e, k); };
				std::string w;
				auto own = files.find(k);
				for (auto& e : passErrors)
				{
					if (about(e, k)) { w = e.substr(k.size() + 2); break; }
					if (own != files.end()) for (auto& f : own->second) if (about(e, f)) { w = e; break; }
					if (!w.empty()) break;
				}
				if (w.empty()) for (auto& e : passErrors) if (!anyGame(e)) { w = e; break; }
				why[k] = w.empty() ? "not brought, for no reason given" : w;
			}
		});
	}
	// a game as the games command prints it, "<system>\t<key>\t<title>\t<bytes>\t<chosen>\t<on the handheld>", "" for one
	// with no key; chosen is on Get games' list (RomM) or ticked (Romhost), on the handheld is on either card (not looked
	// at without look: 0)
	std::string gameLine(const MSystem& s, const MGame& g, const std::set<std::string>& chosen, const std::string& sd2, bool look = true)
	{
		std::string k = gameKey(g);
		if (k.empty()) return "";
		long long bytes = 0;
		bool there = look;
		for (auto& f : g.files)
		{
			if (f.media) continue;
			bytes += std::max(0LL, f.size);
			if (there && !exists(ROMS + "/" + f.path) && (sd2.empty() || !exists(sd2 + "/" + f.path))) there = false;
		}
		std::string t = g.title.empty() ? baseOf(k) : g.title;
		for (char& ch : t) if (ch == '\t' || ch == '\n') ch = ' ';
		return s.folder + "\t" + k + "\t" + t + "\t" + std::to_string(bytes) + "\t" + (chosen.count(k) ? "1" : "0") + "\t" + (there ? "1" : "0") + "\n";
	}
	// RomM's games as Get games shows them, kept on the card (banana/homesync-server-games.tsv) so the page opens at once
	// rather than after the half a minute the full list took on a real server of 742 games: "# <server>\t<unix time>",
	// then the games command's lines, every system's (a system set to Skip included: on RomM that keeps only its saves
	// here), written afresh by the games command and by each sync of the games or every save.
	// The page works out what is waiting or on the card itself, as both change after: the chosen column is as it was
	// when written, and the cards aren't looked at for the other (0), which every sync would otherwise do for each game
	std::string gamesCacheFile() { return ROOT + "/banana/homesync-server-games.tsv"; }
	// the server's games, for Get games, and what was asked of it and why some didn't come: gone with a Forget and with a
	// pairing to another server, whose next sync would otherwise download games asked of this one. The games whose saves
	// stay here are this handheld's, whichever server
	void dropServerLists()
	{
		unlink(gamesCacheFile().c_str());
		unlink(getFile().c_str());
		unlink(getFailedFile().c_str());
	}
	void writeGamesCache(const Conf& c, const Manifest& m)
	{
		auto v = readListLines(getFile());
		std::set<std::string> chosen(v.begin(), v.end());
		std::string sd2 = sd2Roms(), rows;
		for (auto& s : m.systems)
			for (auto& g : s.games) rows += gameLine(s, g, chosen, sd2, false);
		if (!writeAtomic(gamesCacheFile(), "# " + c.server + "\t" + std::to_string((long long)time(nullptr)) + "\n" + rows))
			say("the server's games couldn't be kept on the card (%s)", gamesCacheFile().c_str());
	}
	// RomM: the games whose saves stay on this handheld (the game page's Save sync), "<system folder>/<ROM file name>"
	std::set<std::string> readLocal() { auto v = readListLines(ROOT + "/banana/homesync-local.txt"); return std::set<std::string>(v.begin(), v.end()); }
	std::string localKeyOf(const std::string& romRel)
	{
		size_t s = romRel.find('/');
		return s == std::string::npos || romRel[0] == '/' ? "" : romRel.substr(0, s) + "/" + baseOf(romRel);
	}

	// chosen: null = every game; else only those games come to the card, and the others' files already on it stay (an
	// unticked game isn't removed: un-choosing never deletes, as a skipped system's games stay)
	// fetch (RomM's Get games): the chosen games downloaded once and left to the owner, nothing removed, and only their
	// own gamelist entries written; the server's list isn't followed after (no etag kept)
	// toSd2 (settings homesync_roms_to = sd2): new games go to the second SD card instead of this one. A game the sync
	// already put on a card stays on that card (its updates and its removal happen there); a new game whose files are
	// on either card already (copied by hand) is adopted where it is. With the second card out, the files that belong
	// on it wait for the next sync (an error says so): they never go to this card instead
	RomResult syncRoms(Backend& b, Conf& c, const Manifest& m, const std::set<std::string>& skipFolders, const std::set<std::string>* chosen = nullptr,
		bool toSd2 = false, bool fetch = false)
	{
		RomResult res;
		std::map<std::string, const MFile*> want;
		std::set<std::string> keep;
		std::string sd2 = sd2Roms();
		std::set<std::string> onSd2;	// the wanted files that belong on the second SD card
		std::map<std::string, const MGame*> gameOf;	// each wanted file's game, for the progress line
		for (auto& s : m.systems) for (auto& g : s.games)
		{
			bool on = !chosen || chosen->count(gameKey(g));
			for (auto& f : g.files) { if (on) { want[f.path] = &f; gameOf[f.path] = &g; } else keep.insert(f.path); }
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
			if (fetch) break;	// (a download only: nothing leaves the card)
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
		// the progress line counts games, not files: a game is its ROM, its cover, a PS1 set's discs and .m3u, so the
		// files are put in their games' order (by path a cover, under media/, comes after every ROM of its system) and
		// the line says which game of how many, by its title
		std::map<const MGame*, int> order;
		for (auto* f : todo) order.emplace(gameOf[f->path], (int)order.size());
		std::stable_sort(todo.begin(), todo.end(), [&](const MFile* a, const MFile* b) { return order[gameOf[a->path]] < order[gameOf[b->path]]; });
		const int games = (int)order.size();
		long long doneBytes = 0;
		int gi = 0, fi = 0;
		const MGame* last = nullptr;
		std::set<std::string> got;	// (the files this run downloaded)
		for (auto* f : todo)
		{
			const MGame* g = gameOf[f->path];
			fi++;
			if (g != last) { gi++; last = g; }
			bool sd = onSd2.count(f->path) > 0;
			std::string dst = (sd ? sd2 : ROMS) + "/" + f->path, err;
			std::string label = g && !g->title.empty() ? g->title : baseOf(f->path);
			if (f->media) label += " (cover)";
			const std::string what = games > 1 ? "Downloading game " + std::to_string(gi) + " of " + std::to_string(games) : "Downloading";
			auto show = [&](long long now) {
				long long mb = (doneBytes + now) >> 20, tot = std::max(1LL, need >> 20);
				status("running " + std::to_string(std::min(mb, tot)) + " " + std::to_string(tot) + " " + what + "|" + label);
			};
			show(0);
			progress = [&](long long now, long long) { show(now); };
			bool ok;
			if (f->generated) ok = writeAtomic(dst, f->content);
			else
			{
				std::string algo = f->packed ? "" : !f->sha1.empty() ? "sha1" : !f->md5.empty() ? "md5" : "";
				std::function<std::string(const std::string&)> members;
				if (f->packed && !f->members.empty() && extOf(f->path) == "zip")
					members = [f](const std::string& part) {
						// (a zip whose list of members isn't read here (zip64, a member of 4 GB) is kept unchecked, as a
						// download the server gave no hash for is, rather than thrown away and fetched again every sync;
						// a member the server gave no size is matched by its CRC, and one with no CRC isn't checked)
						auto local = zipMembers(part);
						if (local.empty()) { say("  %s: its members can't be read here, not checked", baseOf(f->path).c_str()); return std::string(); }
						for (auto& m : f->members)
						{
							const std::string crc = lower(m.second);
							if (crc.empty()) continue;
							bool found = false;
							for (auto& l : local) if (l.second == crc && (m.first < 0 || l.first == m.first)) { found = true; break; }
							if (!found) return std::string("checksum mismatch (a member of the zip)");
						}
						return std::string();
					};
				ok = fetchTo(f->url, b.authFor(f->url), dst, f->media ? -1 : f->size, algo, f->packed ? "" : !f->sha1.empty() ? f->sha1 : f->md5, err, nullptr, members);
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
			got.insert(f->path);
			saveConf(c);	// progress kept: a sync cut short goes on from here
			say("  [file %d/%zu] %s", fi, todo.size(), f->path.c_str());
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
				// (a download writes the entries of the games it brought, no other's: a game asked for that was on the
				// card already, copied by hand and adopted, keeps the entry its owner gave it)
				if (fetch && !chosen->count(gameKey(g))) continue;
				if (fetch && std::none_of(g.files.begin(), g.files.end(), [&](const MFile& f) { return !f.media && got.count(f.path); })) continue;
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
				if (fetch && ours[k].empty()) continue;
				// (a download's entries are the owner's once written: not kept to be taken out again)
				std::vector<std::string> none;
				auto& before = fetch ? none : c.gamelist[key];
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
		for (auto it = c.gamelist.begin(); !fetch && it != c.gamelist.end();)
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
		if (res.errors.empty() && !fetch) c.etag = m.etag;
		if (fetch)
		{
			// (each game asked for taken off the list once its files are all in place, or one of them is the owner's own)
			std::set<std::string> done, mine(res.conflicts.begin(), res.conflicts.end());
			for (auto& s : m.systems)
				for (auto& g : s.games)
				{
					std::string k = gameKey(g);
					if (!chosen->count(k)) continue;
					bool in = true, own = false;
					for (auto& f : g.files)
					{
						if (f.media) continue;
						own = own || mine.count(f.path);
						auto st = c.files.find(f.path);
						std::string root = c.where.count(f.path) ? sd2 : ROMS;
						in = in && st != c.files.end() && st->second == f.key() && !root.empty() && exists(root + "/" + f.path);
					}
					if (in || own) done.insert(k);
				}
			// (and one the server no longer lists: nothing to bring)
			std::set<std::string> listedKeys;
			for (auto& s : m.systems) for (auto& g : s.games) listedKeys.insert(gameKey(g));
			for (auto& k : *chosen) if (!listedKeys.count(k)) { done.insert(k); say("  get: %s isn't on the server any more", k.c_str()); }
			if (!dryRun) editGet({}, done);
		}
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
		// (a redirect to https on the same host, which an address typed with http:// and a server behind a proxy that
		// sends everything to https gives: the address it names tried next, as the https try is for a bare one, rather
		// than followed, so the address kept is the one that answers)
		auto hostOf = [](const std::string& u)
		{
			size_t h = u.find("://");
			h = h == std::string::npos ? 0 : h + 3;
			size_t e = u.find_first_of(":/", h);
			return lower(u.substr(h, e == std::string::npos ? std::string::npos : e - h));
		};
		auto moved = [&](const Resp& r, const std::string& from, const std::string& asked)
		{
			auto l = r.hdr.find("location");
			if (r.code < 300 || r.code >= 400 || l == r.hdr.end()) return;
			const std::string& loc = l->second;
			if (lower(loc.substr(0, 8)) != "https://" || loc.size() <= asked.size() || loc.compare(loc.size() - asked.size(), asked.size(), asked) != 0) return;
			std::string base = loc.substr(0, loc.size() - asked.size());
			if (hostOf(base) == hostOf(from) && std::find(tries.begin(), tries.end(), base) == tries.end()) tries.push_back(base);
		};
		for (size_t i = 0; i < tries.size(); i++)
		{
			const std::string u = tries[i];
			// (redirects not followed: a web port that sends everything to https would otherwise answer as the https
			// server, and that port then be kept, every POST to it turned into a GET by the redirect; the https try
			// further down finds the server where it is)
			Req q; q.url = u + "/api/v1/hello"; q.timeout = 5; q.follow = false;
			Resp r = http(q);
			if (tls && (r.curl == CURL_UNTRUSTED || r.curl == CURL_BAD_CA)) *tls = r.curl;
			if (r.code == 0) continue;	// (nothing there, on this scheme and port: the heartbeat wouldn't get further)
			moved(r, u, "/api/v1/hello");
			JPtr j = r.code == 200 ? r.json() : nullptr;
			if (j && j->str("service") == "romhost") { url = u; name = j->str("name"); return "romhost"; }
			q.url = u + "/api/heartbeat";
			r = http(q);
			moved(r, u, "/api/heartbeat");
			j = r.code == 200 ? r.json() : nullptr;
			if (j && j->get("SYSTEM")) { url = u; name = "RomM " + j->get("SYSTEM")->str("VERSION"); return "romm"; }
		}
		return "none";
	}
	// RomM at the base URL given, asked once (its heartbeat), no port guessed and no redirect followed: the answer the
	// form on the handheld shows. ok: "romm"; else why it isn't, and what it found (a redirect's location, a status)
	std::string probeRomm(const std::string& base, std::string& version, std::string& detail)
	{
		Req q; q.url = base + "/api/heartbeat"; q.timeout = 8; q.follow = false;
		Resp r = http(q);
		if (r.code == 0)
		{
			switch (r.curl)
			{
			case 6: return "dns";			// (CURLE_COULDNT_RESOLVE_HOST)
			case 7: return "refused";		// (CURLE_COULDNT_CONNECT)
			case 28: return "timeout";		// (CURLE_OPERATION_TIMEDOUT)
			case CURL_BAD_CA: return "ca";
			case 35: case 51: case 58: case CURL_UNTRUSTED:
			{
				// (a certificate is checked against the clock too, and until Wi-Fi's first time sync (the wifi script
				// writes time-synced) the clock is whatever the RTC kept: a certificate newer than that isn't valid
				// yet. Said as the clock only when the certificate's own dates are what this clock fails, looked at
				// with a second request that takes it unchecked and sends nothing; one simply not trusted is that, with
				// or without the time, so the form offers its way past it. curl's words can't tell the two apart: the
				// image's libcurl has none)
				if (access((RUN + "/time-synced").c_str(), F_OK) == 0) return "tls";
				q.certDates = true;
				Resp d = http(q);
				const long long now = (long long)time(nullptr);
				return d.certFrom && d.certTo && (now < d.certFrom || now > d.certTo) ? "clock" : "tls";
			}
			}
			detail = r.err;
			return "noanswer";
		}
		if (r.code >= 300 && r.code < 400)
		{
			detail = r.hdr.count("location") ? r.hdr["location"] : "";
			std::string host = lower(detail);
			size_t h = host.find("://");
			host = h == std::string::npos ? "" : host.substr(h + 3, host.find('/', h + 3) == std::string::npos ? std::string::npos : host.find('/', h + 3) - h - 3);
			const std::string cf = ".cloudflareaccess.com";
			bool cfa = (host.size() > cf.size() && host.compare(host.size() - cf.size(), cf.size(), cf) == 0)
				|| detail.find("/cdn-cgi/access/") != std::string::npos;
			return cfa ? "access" : "redirect";
		}
		if (r.code == 401 || r.code == 403) { detail = std::to_string(r.code); return "denied"; }
		JPtr j = r.code == 200 ? r.json() : nullptr;
		if (r.code == 200 && !(j && j->get("SYSTEM") && j->get("SYSTEM")->isObj())) return "notromm";
		if (r.code != 200) { detail = std::to_string(r.code); return "http"; }
		version = j->get("SYSTEM")->str("VERSION");
		return "romm";
	}
	// a RomM base URL as typed: a scheme (https when none), no slash at its end
	std::string rommBase(std::string a)
	{
		a = trim(a);
		while (!a.empty() && a.back() == '/') a.pop_back();
		if (a.find("://") == std::string::npos) a = "https://" + a;
		return a;
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
	int pair(const std::string& addr, const std::string& codeIn, bool romm)
	{
		std::string url, name;
		int tls = 0;
		std::string kind;
		if (romm)
		{
			std::string v, detail;
			url = rommBase(addr);
			kind = probeRomm(url, v, detail);
			if (kind != "romm")
			{
				status("error " + kind + (detail.empty() ? "" : " " + detail));
				say("no RomM at %s: %s %s", url.c_str(), kind.c_str(), detail.c_str());
				return 1;
			}
			name = "RomM " + v;
		}
		else kind = probe(addr, url, name, &tls);
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
		std::string code = trim(codeIn), dev = getenv("BANANA_SYNC_NAME") ? getenv("BANANA_SYNC_NAME") : !c.name.empty() ? c.name : deviceName();	// (paired again: the name it was given)
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
			Req q; q.method = "POST"; q.url = url + "/api/v1/pair"; q.body = &b; q.headers = { "Content-Type: application/json" }; q.follow = false;
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
			Req q; q.method = "POST"; q.url = url + "/api/client-tokens/exchange"; q.body = &b; q.headers = { "Content-Type: application/json" }; q.follow = false;
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
			Req q; q.method = "POST"; q.url = url + "/api/auth/device/init"; q.body = &b; q.headers = { "Content-Type: application/json" }; q.follow = false;
			Resp r = http(q);
			JPtr j = (r.code == 200 || r.code == 201) ? r.json() : nullptr;
			if (!j || j->str("device_code").empty()) { status("error " + (r.code == 404 ? std::string("this RomM has no device pairing (update it, or type a client token's code)") : r.error())); return 1; }
			std::string dc = j->str("device_code"), uc = j->str("user_code");
			int interval = std::max(2, (int)j->num("interval", 5)), left = (int)j->num("expires_in", 600);
			// the page's address with the code in it (verification_path_complete): RomM's /pair/device has no field to
			// type a code into, it takes it from ?user_code= only (#141)
			std::string verify = url + j->str("verification_path_complete", j->str("verification_path", "/pair/device") + "?user_code=" + urlEsc(uc));
			status("pair " + uc + " " + verify + " " + std::to_string(left));
			say("On RomM, open %s and enter %s", verify.c_str(), uc.c_str());
			std::string body = "{\"device_code\": " + jsonStr(dc) + "}";
			for (; left > 0; left -= interval)
			{
				sleep(interval);
				Req t; t.method = "POST"; t.url = url + "/api/auth/device/token"; t.body = &body; t.headers = { "Content-Type: application/json" }; t.follow = false;
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
		if (!savePairing(n)) { status("error can't write " + CONF); return 1; }
		if (!(c.server == url && c.backend == kind)) dropServerLists();	// (another server: what was asked of the last one isn't asked of this)
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

	std::string settleKey;
	bool gamesOnly = false;	// sync --games-only: Get games' own run, its downloads and not the saves
	// the commands that change the card's saves with no server: the saves' lock held and no game running, as a sync's
	// save phase; each says "ok", or "error <why>", on its one line, for the menu
	int answer(const std::string& why) { printf("%s\n", why.empty() ? "ok" : ("error " + why).c_str()); return why.empty() ? 0 : 1; }
	std::string lockForSaves()
	{
		if (gameRunning()) return "a game is running";
		if (!lockSaves(10)) return "another sync is saving";
		if (gameRunning()) return "a game is running";
		holdRoms = false; holdSaves = true;
		return "";
	}
	// what its owner can choose for a held note: both (the card's or the server's: a conflict, or a held note that is
	// one, as a state's or a RomM before 5.4's), server (the save here is blank or gone, so only that), none (held back
	// for another reason, a format or a size this handheld can't take). Choosing the server's takes its
	// copy in the vault, so a note with none there offers no such thing. DraStic's and BananaDS's saves apart offer none:
	// the game's next start brings them together, the emulator starting taking its own (nds-saves --prefer)
	bool bothHeld(const SlotNote& n) { return n.why.empty() || n.why.find("changed here and on the server") != std::string::npos; }
	std::string choiceOf(bool isConflict, const SlotNote& n)
	{
		if (n.why == NDS_DIVERGED) return "none";
		bool two = isConflict || bothHeld(n);
		bool gone = lostNote(n);
		if (!two && !gone) return "none";
		if (n.vault.empty() || hashFile(n.vault, "md5") != n.serverMd5) return "none";
		// (the server's a blank save: nothing to put in place from it)
		if (gone && blankSave(readFile(n.vault), saveFolder(n.rel))) return "none";
		return two ? "both" : "server";
	}
	// which of the Saves page's lines a note is: both (changed on both sides, a state's too), gone (missing or blank here
	// since it synced), ds (DraStic's and BananaDS's saves apart, or not brought together), unread (DraStic's save here
	// can't be read), format (the server's save is one this handheld can't take: a format or a size)
	const char* kindOf(bool isConflict, const SlotNote& n)
	{
		if (isConflict || bothHeld(n)) return "both";
		if (lostNote(n)) return "gone";
		if (n.why == NDS_DIVERGED || n.why == NDS_NOT_BROUGHT) return "ds";
		if (n.why == NDS_UNREAD) return "unread";
		return "format";
	}
	// a DS note under key: DraStic's and BananaDS's differ, so which is the card's save isn't known until the game's next
	// start brings them together; no choice over the server's before then
	bool heldForDs(const Conf& c, const std::string& key)
	{
		auto i = c.held.find(key);
		return i != c.held.end() && i->second.why == NDS_DIVERGED;
	}
	// a copy in the vault put in place as the save at rel, in this handheld's form as the sync would take it (a .dsv's
	// footer cut off, the sizes checked), with DraStic's in step. expect: what the save held when this was chosen; rom:
	// the game's path under the ROMs folder, when known
	std::string place(const std::string& rel, const std::string& file, const std::string& serverId, const std::string& expect, const std::string& rom = "")
	{
		bool ok, had = exists(ROOT + "/" + rel);
		std::string data = readFile(file, &ok), local = had ? readFile(ROOT + "/" + rel) : "", why;
		if (!ok) return "that copy can't be read";
		// (a blank save here says nothing of the game's size, as in the sync's download)
		bool sized = had && !blankSave(local, saveFolder(rel));
		if (rel.compare(0, 6, "saves/") == 0) why = adaptIn(saveFolder(rel), data, sized ? &local : nullptr);
		if (!why.empty()) return why;
		std::string dl = RUN + "/sync-dl." + std::to_string(getpid()), err;
		if (!writeAtomic(dl, data)) return "can't write the copy";
		KillHold placing;
		Install in = installSave(rel, dl, serverId, err, &expect);
		if (in != Install::Done) unlink(dl.c_str());
		if (in == Install::Blank) return "that copy is blank, nothing to put in place";
		if (in == Install::Failed) return err;
		if (saveFolder(rel) == "nds" && ndsSaves(rom.empty() ? romOfSave(rel) : romPath(rom)) != 0) return "put in place, but DraStic's save isn't in step with it";
		return gameRunning() ? "a game started while it was placed" : "";
	}
	// resolve <key> server: the server's copy, from the vault, so it needs no network; the next sync finds the two the
	// same and agrees it
	int settleHere(const std::string& key)
	{
		std::string why = lockForSaves();
		if (!why.empty()) return answer(why);
		Conf c = loadConf();
		auto* from = c.conflicts.count(key) ? &c.conflicts : c.held.count(key) ? &c.held : nullptr;
		if (!from) return answer("nothing held for it");
		SlotNote n = (*from)[key];
		std::string can = choiceOf(from == &c.conflicts, n);
		if (heldForDs(c, key)) return answer("DraStic's and BananaDS's saves differ: start the game first");
		if (can == "none") return answer(n.why.empty() || bothHeld(n) ? "the server's copy isn't in the vault" : "not a conflict: " + n.why);
		why = place(n.rel, n.vault, std::to_string(n.saveId), n.localMd5, n.rom);
		if (!why.empty()) return answer(why);
		from->erase(key);
		saveConf(c);
		return answer("");
	}
	// restore <file>: a kept copy as the card's save, the save it replaces kept; the next sync sends it as any change
	int restore(const std::string& arg)
	{
		std::string file = vaultFile(arg), why;
		// (a copy the vault lists, never a file of another kind beside them)
		bool listed = false;
		if (!file.empty()) for (auto& k : vaultList()) if (vaultFile(k.file) == file) listed = true;
		if (!listed) return answer("not a kept copy in the vault");
		why = lockForSaves();
		if (!why.empty()) return answer(why);
		std::string rel;
		for (auto& k : vaultList()) if (vaultFile(k.file) == file) rel = k.rel;
		if (rel.empty()) return answer("not a kept copy of a save");
		std::string p = ROOT + "/" + rel;
		return answer(place(rel, file, "restore", exists(p) ? hashFile(p, "md5") : ""));
	}
	// what the Saves page lists (banana-sync saves), a tab-separated line each: what is held (conflict, held), what waits
	// to be sent (queued), and, with kept, every copy in the vault (kept); onlyRom: that game's alone
	std::string noteLines(const Conf& c, const std::string& onlyRom, bool kept)
	{
		std::string out, only = onlyRom.empty() ? "" : stemOf(baseOf(onlyRom));
		auto mine = [&](const std::string& rel) { return only.empty() || stemOf(baseOf(rel)) == only; };
		auto noteMine = [&](const SlotNote& n) { return n.rom.empty() ? mine(n.rel) : onlyRom.empty() || n.rom == onlyRom; };
		auto clean = [](std::string s) { for (char& ch : s) if (ch == '\t' || ch == '\n') ch = ' '; return s; };
		auto line = [&](const std::vector<std::string>& f) { for (size_t i = 0; i < f.size(); i++) out += (i ? "\t" : "") + f[i]; out += "\n"; };
		auto notes = [&](const char* word, const std::map<std::string, SlotNote>& m)
		{
			for (auto& kv : m)
			{
				const SlotNote& n = kv.second;
				// (one with a slot Settings no longer names: nothing to decide, the next sync drops it, its copy kept)
				if (c.backend == "romm" && !slotOfKey(kv.first).empty() && slotOfKey(kv.first) != saveSlot) continue;
				// (a conflict the last sync left, its game's DS saves apart since: neither choice could be carried out
				// until the game's next start brings the two together, so none is offered, and the line says why)
				bool ds = heldForDs(c, kv.first);
				if (noteMine(n)) line({ word, kv.first, clean(n.rel), clean(n.why), n.localMd5, std::to_string(fileSize(ROOT + "/" + n.rel)),
					n.serverMd5, std::to_string(n.vault.empty() ? -1 : fileSize(n.vault)), clean(n.device), n.updated, clean(n.vault),
					ds ? "none" : choiceOf(word[0] == 'c', n), clean(n.rom), ds ? "ds" : kindOf(word[0] == 'c', n) });
			}
		};
		notes("conflict", c.conflicts);
		notes("held", c.held);
		for (auto& kv : c.queue)
		{
			auto si = c.slots.find(kv.first);
			std::string rel = !kv.second.rel.empty() ? kv.second.rel : si != c.slots.end() ? si->second.rel : "";
			if (mine(rel)) line({ "queued", kv.first, clean(rel), kv.second.md5, clean(kv.second.vault) });
		}
		if (kept)
			for (auto& k : vaultList())
				if (mine(k.rel)) line({ "kept", clean(k.rel), clean(k.file), std::to_string(k.size) });
		return out;
	}

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
			// (before a game starts: RomM's slots only, one game's, under either card's ROMs folder; Romhost's plan is all
			// the saves at once)
			if (autoWhy == "launch" && (onlyRom.empty() || onlyRom[0] == '/' || c.backend != "romm" || get("homesync_launch", "off") != "on")) return 0;
		}
		if (!tlsVerify && c.server.compare(0, 8, "https://") == 0) say("certificate checks are off (homesync_tls)");
		if (c.backend == "romm" && get("homesync_slot", "autosave") == "custom")
		{
			if (saveSlot != "autosave") say("save slot: %s", saveSlot.c_str());
			else if (get("homesync_slot_name", "") != "autosave")
				say("save slot: \"%s\" isn't a slot name (letters, digits, spaces, - and _, up to 64): saves sync with autosave", get("homesync_slot_name", "").c_str());
		}
		bool states = get("homesync_states", "off") == "on" || settleKey.find("/state:") != std::string::npos;
		std::set<std::string> skip;
		for (auto& kv : set) if (kv.first.compare(0, 9, "homesync.") == 0 && kv.second == "off") skip.insert(kv.first.substr(9));
		// RomM: the games come only by Get games (fetch: the games asked for, downloaded once), and a game's saves stay
		// on this handheld when its page says so. Romhost: the card follows the server's list, every game or the chosen
		const bool romm = c.backend == "romm";
		bool pick = !romm && get("homesync_games", "all") == "chosen";
		std::set<std::string> chosen;
		if (romm) { auto v = readListLines(getFile()); chosen.insert(v.begin(), v.end()); c.etag.clear(); }
		else if (pick) { chosen = readChoice(); c.etag.clear(); }	// (the list in full: the choice may have changed since)
		auto b = backendFor(c);
		b->skip = skip;
		b->ownCovers = ownCoversOf(SYSTEMS);
		if (romm) b->local = readLocal();
		b->onlyRom = onlyRom;
		// (one game's sync, before it or after it, for a game whose saves stay here: nothing to do, said at once so the
		// wait before the game is over)
		if (romm && !onlyRom.empty() && b->local.count(localKeyOf(onlyRom)))
		{
			say("%s: its saves stay on this handheld", onlyRom.c_str());
			if (autoWhy == "launch" && !dryRun) writeAtomic(RUN + "/launch.conflict", "");
			// (the menu's banner after a game says nothing for this note: keep the two the same)
			status("done 0 0 0 0 0 0|its saves stay on this handheld");
			return 0;
		}
		b->onlyQueued = queued;
		b->settleKey = settleKey;
		b->beforeGame = autoWhy == "launch";
		if (!onlyRom.empty() && c.backend != "romm") say("Romhost decides which saves move: all of them synced, not only %s's", onlyRom.c_str());
		// (one game's: its states go too, when they are on, except while it waits to start)
		if (!onlyRom.empty()) { savesOnly = true; if (autoWhy == "launch") states = false; }
		bool roms = !savesOnly && (romm ? !chosen.empty() : get("homesync_roms", "on") == "on");
		Awake awake;
		if (roms) awake.take();
		// (the line before the server's first answer: the menu's wait before a game gives that answer 5 s from it)
		const std::string first = "running 0 0 |" + (c.serverName.empty() ? c.server : c.serverName);
		status(first);
		// (RomM, a run of every save or of games: the server asked first, and one that doesn't answer at all (Wi-Fi not
		// back yet, the server down) ends the run there, said as that, rather than as each part failing in turn and the
		// menu saying it didn't all work. One game's sync has its own way with the server out of reach: the vault)
		if (romm && onlyRom.empty())
		{
			Req q; q.url = c.server + "/api/heartbeat"; q.timeout = 8; q.follow = false;
			Resp r = http(q);
			if (r.code == 0)
			{
				std::string why = r.err.empty() ? "no answer" : r.err;
				say("the server can't be reached: %s", why.c_str());
				status("error noconnect " + why);
				return 1;
			}
		}
		std::vector<std::string> errors, conflicts;
		int downloaded = 0, removed = 0, up = 0, down = 0, sconf = 0;
		std::string note;
		auto romsPass = [&]() -> bool
		{
			const size_t before = errors.size();
			// (Get games: what didn't come said per game, for the page)
			Manifest m;
			std::map<std::string, std::vector<std::string>> files;
			auto noteFailed = [&]()
			{
				if (!romm || dryRun) return;
				for (auto& s : m.systems) for (auto& g : s.games)
				{
					std::string k = gameKey(g);
					if (k.empty() || !chosen.count(k)) continue;
					for (auto& f : g.files) if (!f.media) files[k].push_back(f.path);
				}
				noteGetFailed(chosen, std::vector<std::string>(errors.begin() + before, errors.end()), files);
			};
			std::string err;
			if (!b->manifest(m, romm ? std::set<std::string>() : skip, err))
			{
				if (err == "unpaired") { status("error unpaired"); c.last = "unpaired"; saveConf(c); noteFailed(); return false; }
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
				RomResult r = syncRoms(*b, c, m, skip, romm || pick ? &chosen : nullptr, toSd2, romm);
				downloaded += r.downloaded; removed += r.removed; conflicts.insert(conflicts.end(), r.conflicts.begin(), r.conflicts.end());
				for (auto& e : r.errors) errors.push_back(e);
			}
			noteFailed();
			return true;
		};
		// (Get games: what was asked for while this run was going, brought by it too, once each: a game whose download
		// failed waits for the next sync. Before the saves, so a game's save comes with it; after them, the last ones)
		std::set<std::string> tried = chosen;
		auto more = [&]()
		{
			if (!romm || savesOnly || dryRun) return false;
			std::set<std::string> next;
			for (auto& k : readListLines(getFile())) if (tried.insert(k).second) next.insert(k);
			if (next.empty()) return false;
			chosen = next;
			awake.take();
			return true;
		};
		if (roms && !romsPass()) return 1;
		for (int pass = 0; pass < 8 && more(); pass++) if (!romsPass()) return 1;
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
			say("saves: waiting for another sync's to end");
			if (b->beforeGame) status("waiting 0 0 |saves");
			bool had = lockSaves(onlyRom.empty() ? 60 : 10);
			if (had && b->beforeGame) status(first);
			return had;
		};
		// (Get games' own run, --games-only: the games asked for and nothing else, the saves left to the syncs that are for them)
		if (gamesOnly) say("saves: left to the syncs for them (--games-only)");
		else if (gameRunning()) errors.push_back(running);
		else if (!lock()) errors.push_back("saves: another sync is saving, next time");
		// (the lock let go with the saves left alone: kept, it would be held through the games asked for meanwhile, below,
		// and the sync after this game, which waits 10 s for it, would leave the session's save unsent)
		else if (gameRunning()) { unlockSaves(); errors.push_back(running); }
		else
		{
			takeSaves(c, loadConf());
			holdSaves = true;
			// (a run cut short last time: how far each operation got, before anything else touches the saves; a save
			// found in place is agreed in the record, as the run would have done next, and the record written before
			// the journal is closed)
			if (!dryRun)
			{
				std::string rec = journalRecover(&c, [&] { return saveConf(c); });
				for (size_t a = 0, e; (e = rec.find('\n', a)) != std::string::npos; a = e + 1) say("  journal: %s", rec.substr(a, e - a).c_str());
				savesFlatFirst();
			}
			// (the game started again after Power off after sleeping, before it starts: left to its own sync, which its
			// launcher runs when it ends. The resume state bananad saved belongs with the save on the card now)
			std::string resume;
			std::getline(std::ifstream(RUN + "/resume-once"), resume);
			if (resume.empty()) { std::ifstream rb(ROOT + "/banana/resume-boot"); for (std::string l; std::getline(rb, l);) if (l.compare(0, 4, "rom=") == 0) resume = l.substr(4); }
			std::string rr = romsRelative(resume);
			b->resumeRom = !rr.empty() && rr[0] == '/' ? "" : rr;
			b->saves(states, up, down, sconf, errors, note);
		}
		// (the saves' part written and save.lock let go first: the games asked for meanwhile can take minutes, and the
		// sync after a game, or before one, must not wait on them)
		if (holdSaves) { saveConf(c); holdSaves = false; unlockSaves(); }
		for (int pass = 0; pass < 8 && more(); pass++) if (!romsPass()) return 1;
		// (RomM: the server's games, listed for the saves or for Get games' downloads, kept for Get games' page; not
		// from one game's sync, which lists that game's system alone)
		if (romm && onlyRom.empty() && !dryRun && b->haveList())
		{
			Manifest m;
			std::string err;
			if (b->manifest(m, {}, err)) writeGamesCache(c, m);
		}
		for (auto& e : errors) say("  error: %s", e.c_str());
		if (!note.empty()) say("%s", note.c_str());
		char sum[256];
		snprintf(sum, sizeof sum, "%d %d %d %d %d", downloaded, removed, up, down, sconf + (int)conflicts.size());
		c.last = std::string(errors.empty() ? "ok " : "errors ") + sum + (errors.empty() ? "" : "|" + errors[0]);
		c.lastTime = (double)time(nullptr);
		saveConf(c);
		// (before a game: what is held for it, as the Saves page lists it, for the menu's question, written before the
		// done line it reads it after; read off the card, the menu would wait on it)
		if (autoWhy == "launch" && !dryRun) writeAtomic(RUN + "/launch.conflict", noteLines(loadConf(), onlyRom, false));
		// (one game's save left waiting, the server out of reach: said, for the menu's banner after a game)
		std::string said = errors.empty() ? note : errors[0];
		if (b->keptUnsent) said += "; kept to send with the next sync";
		// (Get games' own run says so first, for Sync now's report: its saves weren't looked at)
		if (gamesOnly) said = said.empty() ? "games only" : "games only: " + said;
		status("done " + std::string(sum) + " " + std::to_string(errors.size()) + "|" + said);
		say("done: %d files of games down, %d removed, %d saves up, %d down, %d conflicts, %zu errors", downloaded, removed, up, down, sconf + (int)conflicts.size(), errors.size());
		return errors.empty() ? 0 : 1;
	}
}

int main(int argc, char** argv)
{
	std::vector<std::string> args;
	std::string autoWhy;
	bool savesOnly = false;
	std::string onlyRom;
	std::string report;	// (sync --rom after a game: where its lines go, else <run>/gameend.status)
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
		else if (a == "--games-only") gamesOnly = true;
		else if (a == "--auto") autoWhy = val();
		else if (a == "--rom") onlyRom = val();
		else if (a == "--report") report = val();
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
		saveSlot = slotOf(set["homesync_slot"], set["homesync_slot_name"]);
	}
	if (args.empty()) { fprintf(stderr, "usage: banana-sync discover | probe [--romm] <address> | pair [--romm] <address> [code] | forget | sync [--saves-only] [--rom <path>] [--auto quit|wifi|launch] [--dry-run] | status | games | get [--drop] <key>... | saves [--rom <path>] | resolve <key> card|server | restore <file> | blank <file> [<folder>]\n"); return 2; }
	std::string cmd = args[0];
	// (exit 0: blank, a core's fresh save holding nothing yet; what run-retroarch asks before it seeds the GBA folder)
	if (cmd == "blank" && args.size() > 1)
	{
		bool ok;
		std::string b = readFile(args[1], &ok);
		return !ok ? 2 : blankSave(b, args.size() > 2 ? args[2] : "") ? 0 : 1;
	}
	if (cmd == "resolve" && args.size() > 2 && args[2] != "card")
		return args[2] == "server" ? settleHere(args[1]) : answer("no such choice");
	if (cmd == "restore" && args.size() > 1) return restore(args[1]);
	// banana-sync saves [--rom <path>]: what the Saves page lists, nothing asked of the server, a line each, tab between
	// fields: conflict|held <key> <save> <why> <card's MD5> <card's size> <server's MD5> <server's size> <its device>
	// <its time> <vault copy> <what can be chosen: both, server, none> <the game, when known> <its kind: both (changed
	// on both sides), gone (missing or blank here), ds (DraStic's and BananaDS's apart), unread (DraStic's here can't
	// be read), format (the server's, which this handheld can't take)>; queued <key> <save> <MD5> <vault copy>; kept <save> <file> <bytes>; and with --rom only that game's,
	// by the save's name. The page shows a fixed line per kind; why is for a person reading this
	if (cmd == "saves")
	{
		fputs(noteLines(loadConf(), onlyRom, true).c_str(), stdout);
		return 0;
	}
	if (!cu.load()) { status("error libcurl is missing"); fprintf(stderr, "banana-sync: libcurl is missing\n"); return 1; }
	if (cmd == "discover")
	{
		for (auto& s : discover(2.0)) printf("ROMHOST\t%s\t%s\n", s.first.c_str(), s.second.c_str());
		return 0;
	}
	if (cmd == "probe" && args.size() > 2 && args[1] == "--romm")
	{
		std::string v, detail, url = rommBase(args[2]);
		std::string k = probeRomm(url, v, detail);
		if (k != "romm") { printf("fail %s%s%s\n", k.c_str(), detail.empty() ? "" : " ", detail.c_str()); return 1; }
		printf("romm %s RomM %s%s\n", url.c_str(), v.c_str(), v.empty() || versionAtLeast(v, 5, 4, 0) ? "" : " old");
		return 0;
	}
	if (cmd == "probe" && args.size() > 1)
	{
		std::string url, name;
		int tls = 0;
		std::string k = probe(args[1], url, name, &tls);
		// (nothing found, an https server among the tries refusing for its certificate: "none tls", for the menu to say)
		if (k == "none" && tls == CURL_UNTRUSTED) { printf("none tls\n"); return 1; }
		// (a RomM before 5.4 marked " old", as probe --romm marks it: the menu says what such a server does with saves)
		std::string v = k == "romm" && name.compare(0, 5, "RomM ") == 0 ? name.substr(5) : "";
		printf("%s %s %s%s\n", k.c_str(), url.c_str(), name.c_str(), v.empty() || versionAtLeast(v, 5, 4, 0) ? "" : " old");
		return k == "none";
	}
	if (cmd == "status")
	{
		Conf c = loadConf();
		if (!c.paired()) { printf("not paired\n"); return 0; }
		printf("paired with %s (%s, %s) as %s; %zu files, %zu saves synced; last: %s\n", c.serverName.c_str(), c.server.c_str(), c.backend.c_str(), c.name.c_str(),
			c.files.size(), c.saves.size() + c.states.size(), c.last.c_str());
		long long bytes = 0;
		for (auto& k : vaultList()) bytes += k.size;
		printf("%zu conflicts, %zu held, %zu waiting to be sent; vault %s: %lld bytes\n", c.conflicts.size(), c.held.size(), c.queue.size(), vaultDir().c_str(), bytes);
		return 0;
	}
	// banana-sync games: the server's games for the menu's chooser, a line each: system folder TAB key TAB title TAB
	// bytes TAB chosen (1/0; on RomM, waiting in Get games' list) TAB on the card (1/0); "error <why>" on failure
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
		if (c.backend == "romm") { auto v = readListLines(getFile()); chosen = std::set<std::string>(v.begin(), v.end()); }
		std::string sd2 = sd2Roms();	// (a game on either card is on the handheld)
		for (auto& s : m.systems)
			for (auto& g : s.games) fputs(gameLine(s, g, chosen, sd2).c_str(), stdout);
		if (c.backend == "romm" && !dryRun) writeGamesCache(c, m);
		return 0;
	}
	// banana-sync get <key>...: games added to Get games' list (RomM), for the next sync to bring; get --drop <key>...:
	// taken off it. "ok" or "error <why>"
	if (cmd == "get" && args.size() > 1)
	{
		bool drop = args[1] == "--drop";
		std::vector<std::string> keys(args.begin() + (drop ? 2 : 1), args.end());
		for (auto& k : keys) if (!safeRel(k) || k.find('/') == std::string::npos) return answer("not a game: " + k);
		bool ok = drop ? editGet({}, std::set<std::string>(keys.begin(), keys.end())) : editGet(keys, {});
		return answer(ok ? "" : "the list can't be written");
	}
	// banana-sync rename <name>: the handheld's name in RomM's Devices, set there at once (the device registered if it
	// has none), then kept as the pairing's. "ok" or "error <why>"; nothing changes on an error
	if (cmd == "rename" && args.size() > 1)
	{
		std::string nm = trim(args[1]);
		if (nm.empty() || nm.size() > 63) return answer("a name of 1 to 63 characters");
		// (under romsync.lock from the read to the write: a pairing or a Forget can't land between RomM renamed and the
		// card's name kept, which would leave the two apart. A sync writing its part back meanwhile waits out this one
		// call to the server)
		int lk = lockConf();
		Conf c = loadConf();
		std::string e = !c.paired() ? "not paired" : c.backend != "romm" ? "Homeport names this handheld (ROMs > Devices)" : "";
		if (e.empty()) { c.name = nm; e = registerDevice(c); }
		if (e.empty() && !saveNameLocked(c)) e = "the name can't be kept on the card";
		if (lk >= 0) close(lk);
		return answer(e);
	}
	if (cmd == "forget")
	{
		// (under the state file's lock: a run writing its part back meanwhile would write the pairing back with it)
		int lk = open((RUN + "/romsync.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
		if (lk >= 0) while (flock(lk, LOCK_EX) != 0 && errno == EINTR) {}
		int rc = unlink(CONF.c_str()) != 0 && errno != ENOENT ? 1 : 0;
		unlink((CONF + ".new").c_str());	// (a copy a cut rename left, which the state is otherwise read from)
		if (lk >= 0) close(lk);
		if (rc) return rc;
		dropServerLists();
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
		if (autoWhy != "launch") STATUS = report.empty() ? RUN + "/gameend.status" : report;
		int other = open((RUN + "/sync.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
		bool beside = other >= 0 && flock(other, LOCK_EX | LOCK_NB) != 0;
		if (other >= 0) close(other);
		// (the report on the card for the sync after a game, as for every sync: not beside a sync copying games, whose
		// report it would push aside mid-run, nor for the wait before a game, which reports on launch.status)
		logWanted = !dryRun && autoWhy != "launch" && !beside;
		catchKill(false);
		return doSync(true, autoWhy, onlyRom);
	}
	// resolve <key> card: the card's save kept over the server's. An ordinary run of the saves that knows the choice, so
	// the head is looked at afresh and a 409 is held again; --rom keeps it to that game
	if (cmd == "resolve" && args.size() > 2)
	{
		Conf c = loadConf();
		if (!c.paired()) return answer("not paired");
		auto* from = c.conflicts.count(args[1]) ? &c.conflicts : c.held.count(args[1]) ? &c.held : nullptr;
		if (!from) return answer("nothing held for it");
		SlotNote n = (*from)[args[1]];
		std::string can = choiceOf(from == &c.conflicts, n);
		if (heldForDs(c, args[1])) return answer("DraStic's and BananaDS's saves differ: start the game first");
		if (can != "both" && !bothHeld(n)) return answer("not a conflict: " + n.why);
		if (c.device.empty() && c.backend == "romm") return answer("this handheld's can be kept once it is a RomM device (pair again)");
		settleKey = args[1];
		STATUS = RUN + "/resolve.status";	// (the menu's Sync row is another run's)
		holdRoms = holdSaves = false;
		catchKill(false);
		const int ran = doSync(true, "", onlyRom);
		// (a server out of reach ends the run before it writes anything to the state: said as that, not as the last
		// sync's words)
		const std::string said = trim(readFile(STATUS));
		if (ran != 0 && said.compare(0, 16, "error noconnect ") == 0) return answer("the server can't be reached: " + said.substr(16));
		// (settled only if the card's save is now the agreed version, however the run came to it: a run can end with the
		// note gone and nothing sent)
		Conf a = loadConf();
		std::string md5 = hashFile(ROOT + "/" + n.rel, "md5");
		auto si = a.slots.find(settleKey);
		bool agreed = si != a.slots.end() && si->second.rel == n.rel ? si->second.md5 == md5
			: a.saves.count(n.rel) ? a.saves[n.rel].hash == md5 : a.states.count(n.rel) && a.states[n.rel].hash == md5;
		if (agreed && !a.conflicts.count(settleKey) && !a.held.count(settleKey)) return answer("");
		size_t bar = a.last.find('|');
		return answer(bar != std::string::npos ? a.last.substr(bar + 1) : "held again: the server's changed since");
	}
	int lk = open((RUN + "/sync.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
	// (a sync asked for by hand, the menu's Sync now or Get games' next run, waits a moment for one that is ending: a
	// run writes its done line before it lets go of the lock, and the menu starts the next as it reads that line. On
	// the device a Get games run started there gave up at once, and the game asked for during the last one never came)
	if (lk >= 0 && cmd == "sync" && autoWhy.empty())
		for (int i = 0; i < 100 && flock(lk, LOCK_EX | LOCK_NB) != 0; i++) usleep(100000);
	if (lk >= 0 && flock(lk, LOCK_EX | LOCK_NB) != 0)
	{
		// (and one still running after that: said over the menu's own line, never over that run's)
		if (cmd == "sync" && autoWhy.empty() && trim(readFile(STATUS)) == "running 0 0 |") status("error busy: another sync is running");
		if (autoWhy.empty()) fprintf(stderr, "banana-sync: already running\n");
		// (the pairing screen waits for a line: a pairing started as a sync began isn't left asking for a code. Said
		// only over the menu's own "pairing": a line the running sync wrote is that sync's)
		if (cmd == "pair" && trim(readFile(STATUS)) == "pairing") status("error busy");
		return 3;
	}
	if (cmd == "pair" && args.size() > 1)
	{
		// (its pid as a sync's: the menu's pairing screen stops it with B or X, the device flow waiting up to 10 minutes
		// for RomM's approval; it says "error stopped" as it goes)
		catchKill();
		std::ofstream(RUN + "/sync.pid") << getpid() << "\n";
		bool romm = args[1] == "--romm";
		int rc = romm ? (args.size() > 2 ? pair(args[2], args.size() > 3 ? args[3] : "", true) : 2) : pair(args[1], args.size() > 2 ? args[2] : "", false);
		unlink((RUN + "/sync.pid").c_str());
		return rc;
	}
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
