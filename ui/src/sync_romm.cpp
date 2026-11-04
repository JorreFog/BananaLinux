// banana-sync's RomM backend (sync_romm.h)
#include "sync_romm.h"
#include "save_adapt.h"
#include "save_paths.h"
#include "save_vault.h"
#include <algorithm>
#include <ctime>
#include <dirent.h>
#include <iterator>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

namespace bsync
{
	namespace
	{
	// =============================================================================================================
	// RomM (rommapp/romm 5.x REST API, /api)
	// =============================================================================================================
	// RomM's platform slugs (utils/platform_slugs.py) -> BananaOS's system folders (etc/banana/systems.conf); a
	// platform whose folder (fs_slug) is one of ours is taken as that system too
	const std::map<std::string, std::string> ROMM_SLUGS = {
		{ "nds", "nds" }, { "gba", "gba" }, { "gbc", "gbc" }, { "gb", "gb" }, { "nes", "nes" }, { "famicom", "nes" },
		{ "snes", "snes" }, { "sfam", "snes" }, { "genesis", "megadrive" }, { "genesis-slash-megadrive", "megadrive" },
		{ "megadrive", "megadrive" }, { "psx", "psx" }, { "ps", "psx" }, { "n64", "n64" },
		{ "c64", "c64" }, { "commodore-64", "c64" }, { "commodore64", "c64" },
		{ "zxs", "zxspectrum" }, { "zx-spectrum", "zxspectrum" }, { "zxspectrum", "zxspectrum" },
		{ "g-and-w", "gameandwatch" },
	};
	const std::set<std::string> BANANA_FOLDERS = { "nds", "gba", "gbc", "gb", "nes", "snes", "megadrive", "c64", "zxspectrum", "psx", "n64", "gameandwatch" };
	// one more thing for the sync's note (its last line, and the menu's status when nothing failed)
	void addNote(std::string& note, const std::string& s) { note += (note.empty() ? "" : "; ") + s; }
	// a server ROM: its system (our folder), its stem and file name as RomM has them, the file the ROM sync writes for
	// it (a disc set's .m3u; none for a game the menu can't start), and its files' hashes
	struct RommRom { long long id; std::string folder, stem, fsName, entry; std::vector<RomFile> files; };
	// what a server ROM lists of its files (with_files)
	std::vector<RomFile> romFiles(const JVal& rom)
	{
		std::vector<RomFile> out;
		const JVal* files = rom.get("files");
		if (!files || !files->isArr()) return out;
		for (auto& fj : files->arr)
		{
			RomFile f;
			f.md5 = lower(fj->str("md5_hash")); f.sha1 = lower(fj->str("sha1_hash"));
			if (const JVal* ms = fj->get("archive_members"))
				if (ms->isArr())
					for (auto& m : ms->arr) f.members.push_back({ (long long)m->num("size", -1), lower(m->str("crc_hash")) });
			out.push_back(f);
		}
		return out;
	}
	struct Romm : Backend
	{
		using Backend::Backend;
		std::map<long long, std::string> platforms;	// platform id -> our folder
		std::vector<RommRom> roms;					// every ROM of those platforms (for the saves)
		bool listed = false;
		std::vector<JPtr> romJson;
		bool list(const std::set<std::string>& skip, std::string& err)
		{
			if (listed) return true;
			Resp r = call("GET", "/api/platforms");
			if (r.code != 200) { err = r.code == 401 || r.code == 403 ? "unpaired" : "platforms: " + r.error(); return false; }
			JPtr j = r.json();
			if (!j || !j->isArr()) { err = "the server's platforms can't be read"; return false; }
			for (auto& p : j->arr)
			{
				auto it = ROMM_SLUGS.find(p->str("slug"));
				std::string folder = it != ROMM_SLUGS.end() ? it->second : BANANA_FOLDERS.count(p->str("fs_slug")) ? p->str("fs_slug") : "";
				if (folder.empty() || skip.count(folder)) continue;
				platforms[(long long)p->num("id")] = folder;
			}
			for (auto& pf : platforms)
			{
				for (long long off = 0;; off += 500)
				{
					// (with_files: each ROM's files[], with their hashes and a disc set's discs; RomM 5.4 lists none without it)
					std::string q = "/api/roms?platform_ids=" + std::to_string(pf.first) + "&platform_id=" + std::to_string(pf.first) +
						"&limit=500&offset=" + std::to_string(off) + "&order_by=name&with_files=true&with_char_index=false&with_filter_values=false&with_rom_id_index=false";
					Resp rr = call("GET", q);
					if (rr.code != 200) { err = "roms: " + rr.error(); return false; }
					JPtr page = rr.json();
					const JVal* items = page ? (page->isArr() ? page.get() : page->get("items")) : nullptr;
					if (!items || !items->isArr()) { err = "the server's games can't be read"; return false; }
					for (auto& it : items->arr)
					{
						if ((long long)it->num("platform_id") != pf.first) continue;
						romJson.push_back(it);
						std::string fsName = it->str("fs_name");
						const JVal* files = it->get("files");
						bool multi = it->flag("has_multiple_files") || (files && files->isArr() && files->arr.size() > 1);
						// (as manifest() below writes it: a disc set is its .m3u, another game of several files none)
						std::string entry = !multi ? fsName : pf.second == "psx" ? fsName + ".m3u" : "";
						roms.push_back({ (long long)it->num("id"), pf.second, it->str("fs_name_no_ext", stemOf(fsName)), fsName, entry, romFiles(*it) });
					}
					long long total = page->isArr() ? (long long)items->arr.size() : (long long)page->num("total", -1);
					if (page->isArr() || items->arr.size() < 500 || (total >= 0 && off + 500 >= total)) break;
				}
			}
			listed = true;
			return true;
		}
		bool manifest(Manifest& m, const std::set<std::string>& skip, std::string& err) override
		{
			if (!list(skip, err)) return false;
			std::map<std::string, MSystem> by;
			for (auto& f : platforms) by[f.second].folder = f.second;
			// Game & Watch: the games' MAME artwork zips, in the platform's artwork folder, which RomM lists as one ROM of
			// several files (its files only on its own page): each zip goes with its game (gameandwatch/artwork/<game>.zip,
			// run-gw's place for it), so a chosen game brings its artwork and the folder is no game of its own
			std::map<std::string, MFile> gwArt;
			for (auto& it : romJson)
			{
				std::string folder = platforms[(long long)it->num("platform_id")], fsName = it->str("fs_name");
				long long id = (long long)it->num("id");
				if (folder == "gameandwatch" && fsName == "artwork") continue;	// (its zips go with their games: above)
				if (!safeRel(folder + "/" + fsName) || it->flag("missing_from_fs")) { m.skipped++; continue; }
				MGame g;
				g.title = it->str("name", fsName);
				const JVal* files = it->get("files");
				std::string entry = fsName;
				bool multi = it->flag("has_multiple_files") || (files && files->isArr() && files->arr.size() > 1);
				if (!multi)
				{
					MFile f;
					f.path = folder + "/" + fsName;
					f.size = (long long)it->num("fs_size_bytes", -1);
					if (files && files->isArr() && files->arr.size() == 1)
					{
						f.sha1 = lower(files->arr[0]->str("sha1_hash")); f.md5 = lower(files->arr[0]->str("md5_hash"));
						if (files->arr[0]->get("file_size_bytes")) f.size = (long long)files->arr[0]->num("file_size_bytes");
					}
					f.url = c.server + "/api/roms/" + std::to_string(id) + "/content/" + urlEsc(fsName);
					g.files.push_back(f);
				}
				else if (folder == "psx" && files && files->isArr())
				{
					// a disc set: its files in psx/.discs/<name>/, an .m3u of its discs as the game (Romhost's layout)
					std::string m3u;
					std::vector<std::string> discs;
					for (auto& fj : files->arr)
					{
						std::string fn = fj->str("file_name");
						if (fn.empty() || !safeRel(fn)) continue;
						std::string ext = extOf(fn);
						if (ext == "m3u" || ext == "txt" || ext == "pdf" || ext == "jpg" || ext == "png") continue;
						MFile f;
						f.path = "psx/.discs/" + fsName + "/" + fn;
						f.size = (long long)fj->num("file_size_bytes", -1);
						f.sha1 = lower(fj->str("sha1_hash")); f.md5 = lower(fj->str("md5_hash"));
						f.url = c.server + "/api/roms/" + std::to_string((long long)fj->num("id")) + "/files/content/" + urlEsc(fn);
						g.files.push_back(f);
						if (ext == "cue" || ext == "chd" || ext == "pbp" || ext == "iso") discs.push_back(".discs/" + fsName + "/" + fn);
					}
					if (discs.empty()) { m.skipped++; continue; }
					std::sort(discs.begin(), discs.end());
					for (auto& d : discs) m3u += d + "\n";
					MFile mf; mf.path = "psx/" + fsName + ".m3u"; mf.generated = true; mf.content = m3u; mf.size = (long long)m3u.size();
					g.files.push_back(mf);
					entry = fsName + ".m3u";
				}
				else { m.skipped++; continue; }	// (a game of several files on a cartridge system: not one the menu can start)
				std::string stem = stemOf(entry);
				if (folder == "gameandwatch")
				{
					auto art = gwArt.find(stem);
					if (art != gwArt.end()) g.files.push_back(art->second);
				}
				// the cover, as the menu's library reads it (media/box/<rom name>.png or .jpg), unless the system makes its
				// own (systems.conf: covers = own; Game & Watch's gw-meta, from the artwork); one already on the card is kept
				std::string cover = ownCovers.count(folder) ? std::string() : it->str("path_cover_large");
				std::string cext = extOf(cover.substr(0, cover.find('?')));
				std::string image;
				if (!cover.empty() && (cext == "png" || cext == "jpg" || cext == "jpeg"))
				{
					MFile f;
					f.media = true;
					f.path = folder + "/media/box/" + stem + "." + (cext == "png" ? "png" : "jpg");
					f.url = urlFix(cover.compare(0, 4, "http") == 0 ? cover : c.server + cover);
					if (safeRel(f.path)) { g.files.push_back(f); image = "./media/box/" + stem + "." + (cext == "png" ? "png" : "jpg"); }
				}
				g.gl.push_back({ "path", "./" + entry });
				g.gl.push_back({ "name", it->str("name", stem) });
				if (!it->str("summary").empty()) g.gl.push_back({ "desc", it->str("summary") });
				if (!image.empty()) g.gl.push_back({ "image", image });
				if (const JVal* md = it->get("metadatum"))
				{
					if (const JVal* gs = md->get("genres")) if (gs->isArr() && !gs->arr.empty()) g.gl.push_back({ "genre", gs->arr[0]->s });
					double d = md->num("first_release_date", 0);
					if (d > 0)
					{
						time_t t = (time_t)(d > 1e11 ? d / 1000 : d);	// (ms in RomM's metadatum, seconds in IGDB's)
						char b[32]; strftime(b, sizeof b, "%Y%m%dT000000", gmtime(&t));
						g.gl.push_back({ "releasedate", b });
					}
					std::string pub = md->get("publishers") && md->get("publishers")->isArr() && !md->get("publishers")->arr.empty() ? md->get("publishers")->arr[0]->s : "";
					if (!pub.empty()) g.gl.push_back({ "publisher", pub });
				}
				by[folder].games.push_back(g);
			}
			for (auto& kv : by) m.systems.push_back(kv.second);
			return true;
		}
		// RomM keeps a save per ROM (and per file name). A game's save is the one file its emulator reads
		// (save_paths.h): saves/nds/<stem>.sav, saves/gba/<stem>.srm, saves/<core's name>/<stem>.srm, for a game on the
		// card whose ROM is the server's by its contents; a server save of that ROM in that file's format is that file's.
		// The decision, per save: changed here only -> upload; changed on the server only -> download; changed on both
		// -> the newer one wins and the other is kept on the card as <file>.<side>-<date>.bak (never lost); the same
		// on both -> nothing. A save deleted here is forgotten, not deleted on the server. A blank save here (a core's
		// fresh one) is never sent: it takes the server's when it was never synced, and is left alone when it was
		// (blanked since). Every download is checked against the server's MD5 and installed through the vault.
		struct Canon { RommRom rom; std::string romRel; };
		std::map<std::string, Canon> canonical;		// data-relative save path -> its game
		std::map<long long, std::string> saveOf;	// ROM id -> its save's path
		// each game on the card with a save in play (one on the server, or one here in any folder under saves/), and
		// its save's path. What can't be said for sure is said instead: a system whose save folder isn't known, a ROM
		// other than the server's
		void resolve(const std::set<long long>& onServer, const std::set<std::string>& stemsHere, std::vector<std::string>& errors, std::string& note)
		{
			canonical.clear(); saveOf.clear();
			std::map<std::string, SysSaves> sys;
			std::map<std::string, std::map<std::string, std::string>> index;	// system -> its ROMs on the card
			std::set<std::string> told, shared;
			int unmatched = 0;
			for (auto& r : roms)
			{
				if (r.entry.empty()) continue;
				if (!index.count(r.folder))
				{
					std::string f = romFolder(r.folder);
					index[r.folder] = f.empty() ? std::map<std::string, std::string>() : romIndex(f);
				}
				auto li = index[r.folder].find(r.entry);
				if (li == index[r.folder].end()) continue;	// (not on this card)
				if (!sys.count(r.folder)) sys[r.folder] = systemSaves(r.folder);
				const SysSaves& ss = sys[r.folder];
				std::string stem = stemOf(baseOf(li->second));
				if (ss.folder.empty())
				{
					if ((onServer.count(r.id) || stemsHere.count(stem)) && told.insert(r.folder).second) errors.push_back("saves: " + ss.err);
					continue;
				}
				std::string rel = ss.rel(stem);
				if (!onServer.count(r.id) && !stemsHere.count(stem)) continue;	// (no save in play: not hashed)
				if (!romMatches(li->second, r.files, c))
				{
					unmatched++;
					say("  not synced, not the server's ROM (other bytes): %s", li->second.c_str());
					continue;
				}
				if (canonical.count(rel)) { shared.insert(rel); continue; }
				canonical[rel] = { r, li->second };
				saveOf[r.id] = rel;
			}
			for (auto& rel : shared)
			{
				say("  not synced, two games' save: %s", rel.c_str());
				saveOf.erase(canonical[rel].rom.id);
				canonical.erase(rel);
			}
			savePathCaches();
			if (unmatched) addNote(note, std::to_string(unmatched) + (unmatched == 1 ? " game" : " games") + " not matched on the server");
		}
		// the same game's save in another folder, kept in the vault, said once, never sent and never removed: a core's
		// folder from before 0.5.3's flat saves that saves-flat hasn't emptied, and for a GBA game a flat
		// saves/<stem>.srm (a 0.5.0 download left one there; it may as well be another system's game of the same name)
		void keepOthers(const std::string& rel, std::vector<std::string>& errors)
		{
			std::string folder = saveFolder(rel), name = baseOf(rel);
			if (folder == "nds") return;
			std::vector<std::string> dirs;
			if (folder == "gba") dirs.push_back("");
			if (DIR* d = opendir((ROOT + "/saves").c_str()))
			{
				while (dirent* e = readdir(d))
				{
					std::string n = e->d_name;
					if (n[0] != '.' && n != folder && n != "nds" && n != "gba") dirs.push_back(n + "/");
				}
				closedir(d);
			}
			std::sort(dirs.begin(), dirs.end());
			for (auto& d : dirs)
			{
				std::string o = "saves/" + d + name, p = ROOT + "/" + o;
				struct stat st;
				if (stat(p.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
				bool ok;
				std::string data = readFile(p, &ok);
				if (!ok || blankSave(data, saveFolder(o))) continue;
				if (dryRun) { say("  keep in the vault %s", o.c_str()); continue; }
				std::string kept, err;
				bool added = false;
				if (!vaultKeep(o, p, kept, err, &added)) { errors.push_back(o + ": " + err); continue; }
				if (added) say(d.empty() ? "  an earlier download, kept in the vault, not synced: %s" : "  also here, kept in the vault, not synced: %s", o.c_str());
			}
		}
		// RomM's version as it is now (updated since the pairing, perhaps): before 5.4, whose slots and device sync records
		// the other clients sync by, saves only come down
		bool sendOk = true;
		void checkVersion()
		{
			Resp r = call("GET", "/api/heartbeat");
			JPtr j = r.code == 200 ? r.json() : nullptr;
			const JVal* sys = j ? j->get("SYSTEM") : nullptr;
			if (sys && !sys->str("VERSION").empty() && sys->str("VERSION") != c.serverVersion)
			{
				c.serverVersion = sys->str("VERSION");
				c.serverName = "RomM " + c.serverVersion;	// (as pairing names it)
				saveConf(c);
			}
			sendOk = c.serverVersion.empty() || versionAtLeast(c.serverVersion, 5, 4, 0);
		}
		// this card's device as RomM has it now: none, or gone (deleted in RomM's web UI) -> registered (again); save sync
		// switched off for it there -> no saves this run (false)
		bool checkDevice(std::string& note)
		{
			if (!c.device.empty())
			{
				Resp r = call("GET", "/api/devices/" + urlEsc(c.device));
				if (r.code == 200)
				{
					JPtr j = r.json();
					if (j && j->isObj() && !j->flag("sync_enabled", true)) { addNote(note, "save sync is off for this handheld in RomM (Devices)"); return false; }
					return true;
				}
				if (r.code == 403) { addNote(note, "this pairing can't see its RomM device (pair again)"); return true; }
				if (r.code != 404) return true;	// (the server's trouble: the saves' own calls say it)
				say("  the device was gone from RomM");
				c.device.clear();
			}
			if (dryRun) { say("  would register as a RomM device"); return true; }
			std::string e = registerDevice(c);
			if (!e.empty()) { addNote(note, "not registered as a RomM device (" + e + ")"); return true; }
			say("  registered as RomM device %s", c.device.c_str());
			saveConf(c);
			// (allow_existing may give back a device RomM already had for this handheld, with save sync off for it: read
			// as the one held is)
			Resp r = call("GET", "/api/devices/" + urlEsc(c.device));
			JPtr j = r.code == 200 ? r.json() : nullptr;
			if (j && j->isObj() && !j->flag("sync_enabled", true)) { addNote(note, "save sync is off for this handheld in RomM (Devices)"); return false; }
			return true;
		}
		// states, still matched by name: states/<rom name>.<ext> (states/nds/... for the DS)
		bool matchStateRom(const std::string& rel, RommRom& out)
		{
			// states/<folder>/<name>.<ext> or states/<name>.<ext>
			size_t s1 = rel.find('/');
			std::string rest = rel.substr(s1 + 1), folder, name = baseOf(rel);
			if (rest.find('/') != std::string::npos) folder = rest.substr(0, rest.find('/'));
			std::string stem = stemOf(name);
			std::vector<RommRom*> c1;
			for (auto& r : roms)
				if (r.stem == stem && (folder.empty() ? r.folder != "nds" : r.folder == folder)) c1.push_back(&r);
			if (c1.size() > 1)	// (the same name on two systems: the one on the card)
			{
				std::vector<RommRom*> c2;
				std::string sd2 = sd2Roms();
				for (auto* r : c1)
				{
					std::string rel = r->folder + "/" + r->fsName;
					if (exists(ROMS + "/" + rel) || (!sd2.empty() && exists(sd2 + "/" + rel)))
					{
						c2.push_back(r);
					}
				}
				c1 = c2;
			}
			if (c1.size() != 1) return false;
			out = *c1[0];
			return true;
		}
		bool saves(bool statesToo, int& up, int& down, int& conflicts, std::vector<std::string>& errors, std::string& note) override
		{
			std::string err;
			if (!list(skip, err)) { errors.push_back("saves: " + err); return false; }
			checkVersion();
			if (!sendOk) addNote(note, "RomM " + c.serverVersion + " only gives saves: update RomM to 5.4 or newer to send this handheld's");
			if (!checkDevice(note)) return true;
			ndsSavesFirst();
			auto local = localSaves(statesToo);
			bool ok = sync("save", local, c.saves, up, down, conflicts, errors, note);
			if (statesToo) ok &= sync("state", local, c.states, up, down, conflicts, errors, note);
			return ok;
		}
		bool sync(const std::string& kind, const std::map<std::string, std::string>& localAll, std::map<std::string, SaveSt>& state,
			int& up, int& down, int& conflicts, std::vector<std::string>& errors, std::string& note)
		{
			bool forSaves = kind == "save";
			std::string api = forSaves ? "/api/saves" : "/api/states", field = forSaves ? "saveFile" : "stateFile";
			std::string dev = c.device.empty() ? "" : "device_id=" + urlEsc(c.device);
			// the server's, per platform
			struct Srv { long long id, rom; std::string name, hash, updated; };
			std::map<long long, RommRom> romById;
			for (auto& r : roms) romById[r.id] = r;
			std::vector<std::pair<JPtr, const RommRom*>> rows;
			std::set<long long> onServer;
			for (auto& pf : platforms)
			{
				Resp r = call("GET", api + "?platform_id=" + std::to_string(pf.first) + (forSaves && !dev.empty() ? "&" + dev : ""));
				if (r.code != 200) { errors.push_back(kind + "s: " + r.error()); return false; }
				JPtr j = r.json();
				if (!j || !j->isArr()) continue;
				for (auto& s : j->arr)
				{
					auto ri = romById.find((long long)s->num("rom_id"));
					if (ri == romById.end() || s->flag("missing_from_fs")) continue;
					if (chosen && !chosen->count(ri->second.folder + "/" + ri->second.fsName)) continue;	// (not chosen: not pulled)
					if (!s->str("slot").empty()) continue;	// (another client's versioned slots: left to it)
					rows.push_back({ s, &ri->second });
					onServer.insert(ri->second.id);
				}
			}
			if (forSaves)
			{
				std::set<std::string> stemsHere;
				for (auto& kv : localAll) if (kv.second == "save") stemsHere.insert(stemOf(baseOf(kv.first)));
				resolve(onServer, stemsHere, errors, note);
				for (auto& kv : canonical) keepOthers(kv.first, errors);
				// (0.5.0's flat saves/<name>, which no emulator reads, forgotten: the file itself is kept in the vault above)
				for (auto it = state.begin(); it != state.end();)
					it = it->first.find('/', 6) == std::string::npos ? state.erase(it) : std::next(it);
			}
			std::map<std::string, Srv> srv;	// card path -> the server's newest save of that ROM for that path
			std::set<std::string> notTaken;
			for (auto& row : rows)
			{
				const JPtr& s = row.first;
				const RommRom& rom = *row.second;
				std::string name = s->str("file_name"), rel;
				if (!safeRel(name) || name.find('/') != std::string::npos) continue;
				if (forSaves)
				{
					auto so = saveOf.find(rom.id);
					if (so == saveOf.end()) continue;	// (not on this card, or not the server's ROM: said above)
					if (stemOf(name) != rom.stem && stemOf(name) != stemOf(baseOf(so->second))) continue;
					if (extOf(name) != extOf(so->second))
					{
						if (notTaken.insert(name).second) say("  not taken yet, a format this handheld doesn't read: %s", name.c_str());
						continue;
					}
					rel = so->second;
				}
				else
				{
					if (stemOf(name) != rom.stem) continue;
					rel = rom.folder == "nds" ? "states/nds/" + name : "states/" + name;
				}
				Srv v{ (long long)s->num("id"), rom.id, name, lower(s->str("content_hash")), s->str("updated_at") };
				auto e = srv.find(rel);
				if (e == srv.end() || e->second.updated < v.updated) srv[rel] = v;
			}
			std::set<std::string> paths;
			for (auto& kv : localAll)
				if (kv.second == kind && kv.first.compare(0, forSaves ? 6 : 7, forSaves ? "saves/" : "states/") == 0 && (!forSaves || canonical.count(kv.first)))
					paths.insert(kv.first);
			for (auto& kv : srv) paths.insert(kv.first);
			for (auto& kv : state) if (!forSaves || canonical.count(kv.first)) paths.insert(kv.first);
			int n = 0, total = (int)paths.size();
			for (auto& rel : paths)
			{
				n++;
				status("running " + std::to_string(n) + " " + std::to_string(total) + (forSaves ? " Saves|" : " States|") + baseOf(rel));
				std::string p = ROOT + "/" + rel;
				bool here = localAll.count(rel) > 0;
				auto si = srv.find(rel);
				auto st = state.find(rel);
				std::string md5 = here ? hashFile(p, "md5") : "";
				if (here && !md5.empty() && blankSave(readFile(p), saveFolder(rel)) && (st == state.end() || st->second.hash != md5))
				{
					if (st != state.end()) { say("  blank here since the last sync, left as it is: %s", rel.c_str()); continue; }
					if (si == srv.end() || si->second.hash == md5) continue;
					SaveSt ns; ns.hash = md5;	// (what the download checks it is replacing)
					if (download(api, si->second, rel, ns, dev, false, errors)) { state[rel] = ns; down++; }
					continue;
				}
				if (!here)
				{
					if (st != state.end()) { state.erase(st); continue; }	// deleted here: forgotten (the server keeps its copy)
					if (si == srv.end()) continue;
					if (exists(p)) continue;	// (a file the rules leave out)
					SaveSt ns;
					if (download(api, si->second, rel, ns, dev, false, errors)) { state[rel] = ns; down++; }
					continue;
				}
				if (md5.empty()) continue;
				RommRom rom;
				if (si == srv.end())
				{
					if (st != state.end() && st->second.version > 0 && !st->second.server.empty())
					{
						// it was on the server and is gone: deleted there on purpose (RomM's web UI); not sent back
						// until it changes here
						if (st->second.hash == md5) continue;
					}
					if (forSaves) rom = canonical[rel].rom;
					else if (!matchStateRom(rel, rom)) continue;	// (no ROM of this name on the server)
					if (upload(api, field, rom.id, p, rel, md5, state, dev, false, errors)) up++;
					continue;
				}
				const Srv& s = si->second;
				bool known = st != state.end();
				bool localChanged = !known || st->second.hash != md5;
				bool serverChanged = !known || st->second.version != s.id || (st->second.server != s.updated && (s.hash.empty() || s.hash != st->second.hash));
				if (!s.hash.empty() && s.hash == md5)
				{
					state[rel] = { (double)s.id, md5, s.updated };
					continue;
				}
				if (!localChanged && !serverChanged) continue;
				if (localChanged && !serverChanged) { if (upload(api, field, s.rom, p, rel, md5, state, dev, false, errors)) up++; continue; }
				if (!localChanged && serverChanged) { SaveSt ns = st->second; if (download(api, s, rel, ns, dev, false, errors)) { state[rel] = ns; down++; } continue; }
				// both changed (or a card that never synced holding another version): the newer wins, the other kept
				conflicts++;
				if (dryRun) { say("  conflict %s", rel.c_str()); continue; }
				struct tm tmv = {};
				time_t srvT = strptime(s.updated.substr(0, 19).c_str(), "%Y-%m-%dT%H:%M:%S", &tmv) ? timegm(&tmv) : 0;
				std::string stamp = isoNow();
				if (sendOk && fileTime(p) >= srvT)
				{
					// the card's is newer: the server's kept here beside it, then the card's uploaded over it
					std::string keep = p + ".server-" + stamp + ".bak", e2, want = serverHash(api, s);
					if (want.empty()) { errors.push_back(rel + ": the server gives no checksum for its copy"); continue; }
					if (!fetchTo(c.server + api + "/" + std::to_string(s.id) + "/content", auth(), keep, -1, "md5", want, e2)) { errors.push_back(rel + ": " + e2); continue; }
					if (upload(api, field, s.rom, p, rel, md5, state, dev, true, errors)) { up++; say("  conflict: the card's (newer) kept, the server's saved as %s", baseOf(keep).c_str()); }
				}
				else
				{
					std::string keep = p + ".card-" + stamp + ".bak";
					if (link(p.c_str(), keep.c_str()) != 0) { std::string d = readFile(p); if (!writeAtomic(keep, d)) { errors.push_back(rel + ": can't keep the card's copy"); continue; } }
					SaveSt ns;
					if (download(api, s, rel, ns, dev, true, errors)) { state[rel] = ns; down++; say("  conflict: the server's (newer) taken, the card's kept as %s", baseOf(keep).c_str()); }
				}
				saveConf(c);
			}
			saveConf(c);
			return true;
		}
		bool upload(const std::string& api, const std::string& field, long long romId, const std::string& p, const std::string& rel,
			const std::string& md5, std::map<std::string, SaveSt>& state, const std::string& dev, bool overwrite, std::vector<std::string>& errors)
		{
			if (!sendOk) return false;	// (an older RomM: kept here, said once in the note)
			if (dryRun) { say("  upload %s", rel.c_str()); return true; }
			// a copy of the file as it is now: the emulator can't change what is sent halfway
			std::string snap = RUN + "/sync-upload." + std::to_string(getpid());
			std::string body = readFile(p);
			if (blankSave(body, saveFolder(rel))) { say("  blank, not sent: %s", rel.c_str()); return false; }
			if (hashStr(body, "md5") != md5 || !writeAtomic(snap, body)) { errors.push_back(rel + ": changed while syncing, next time"); unlink(snap.c_str()); return false; }
			Req q; q.method = "POST"; q.headers = auth(); q.timeout = 0;
			q.url = c.server + api + "?rom_id=" + std::to_string(romId) + (api == "/api/saves" && !dev.empty() ? "&" + dev : "") + (overwrite ? "&overwrite=true" : "") + "&emulator=" + urlEsc(baseOf(dirOf(rel)) == "nds" ? "bananaos-nds" : "retroarch");
			q.fileParts.push_back({ field, snap });
			q.partName = baseOf(rel);
			Resp r = http(q);
			unlink(snap.c_str());
			if (r.code == 409) { errors.push_back(rel + ": changed on the server meanwhile, next time"); return false; }
			JPtr j = r.json();
			if ((r.code != 200 && r.code != 201) || !j || !j->isObj()) { errors.push_back(rel + ": " + r.error()); return false; }
			state[rel] = { j->num("id"), md5, j->str("updated_at") };
			vaultGood(rel, body);
			say("  up %s", rel.c_str());
			saveConf(c);
			return true;
		}
		// the MD5 the server has for a save: the listing's, else the save's own record
		template <class S> std::string serverHash(const std::string& api, const S& s)
		{
			if (!s.hash.empty()) return s.hash;
			Resp r = call("GET", api + "/" + std::to_string(s.id));
			JPtr j = r.code == 200 ? r.json() : nullptr;
			return j && j->isObj() ? lower(j->str("content_hash")) : "";
		}
		template <class S> bool download(const std::string& api, const S& s, const std::string& rel, SaveSt& st, const std::string& dev, bool force, std::vector<std::string>& errors)
		{
			std::string p = ROOT + "/" + rel;
			if (dryRun) { say("  download %s", p.c_str()); return false; }
			if (!force && exists(p) && hashFile(p, "md5") != st.hash) { errors.push_back(baseOf(p) + ": changed while syncing, next time"); return false; }
			std::string want = serverHash(api, s), err;
			if (want.empty()) { errors.push_back(baseOf(p) + ": the server gives no checksum for it, not taken"); return false; }
			// (fetched to /run first: nothing on the card changes until it is all here and checked)
			std::string url = c.server + api + "/" + std::to_string(s.id) + "/content" + (api == "/api/saves" && !dev.empty() ? "?" + dev : "");
			std::string dl = RUN + "/sync-dl." + std::to_string(getpid());
			Install in = fetchTo(url, auth(), dl, -1, "md5", want, err) ? installSave(rel, dl, std::to_string(s.id), err) : Install::Failed;
			if (in == Install::Blank) { say("  blank on the server, not taken: %s", baseOf(p).c_str()); return false; }
			if (in == Install::Failed)
			{
				unlink(dl.c_str());
				errors.push_back(baseOf(p) + ": " + err);
				return false;
			}
			st = { (double)s.id, want, s.updated };
			say("  down %s", p.c_str());
			// (DraStic's .dsv brought into step with the DS save just installed, before either emulator starts)
			auto ci = canonical.find(rel);
			if (ci != canonical.end() && saveFolder(rel) == "nds") ndsSaves(ROMS + "/" + ci->second.romRel);
			saveConf(c);
			return true;
		}
	};
	}

	std::unique_ptr<Backend> newRomm(Conf& c) { return std::unique_ptr<Backend>(new Romm(c)); }
}
