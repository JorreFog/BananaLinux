// banana-sync's RomM backend (sync_romm.h)
#include "sync_romm.h"
#include "save_adapt.h"
#include "save_paths.h"
#include "save_vault.h"
#include <algorithm>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
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
	// a ROM's name for the lists (Choose games, the library): RomM's title, then what its file name says that the
	// title doesn't and that sets one version of a game apart from another, its revision and other tags (a hack's name
	// and version, a translation, a beta), as RomM parses them from the file name; with no title, the file's own name,
	// its tags in it already. The regions (on nearly every name of a No-Intro set) only where two games of a system
	// would read the same without them, and a "(v1.1)", which RomM's listing doesn't give, by the file names then
	std::string romTitle(const JVal& it, const std::string& stem)
	{
		std::string t = it.str("name");
		if (t.empty()) return stem;
		if (!it.str("revision").empty()) t += " (Rev " + it.str("revision") + ")";
		if (const JVal* a = it.get("tags"))
			if (a->isArr()) for (auto& g : a->arr) if (g->isStr() && !g->s.empty()) t += " [" + g->s + "]";
		return t;
	}
	std::string romRegions(const JVal& it)
	{
		std::string r;
		if (const JVal* a = it.get("regions"))
			if (a->isArr()) for (auto& e : a->arr) if (e->isStr() && !e->s.empty()) r += (r.empty() ? "" : ", ") + e->s;
		return r;
	}
	// a file RomM reads into to hash (as its roms handler does: these archives, and a gzip or bzip2 file), so that its
	// hashes are of what's inside
	bool packedName(const std::string& fsName)
	{
		std::string e = extOf(fsName);
		return e == "zip" || e == "7z" || e == "rar" || e == "tar" || e == "tgz" || e == "tbz2" || e == "txz" || e == "gz" || e == "bz2" ||
			(e == "xz" && extOf(stemOf(fsName)) == "tar");
	}
	struct Romm : Backend
	{
		using Backend::Backend;
		std::map<long long, std::string> platforms;	// platform id -> our folder
		std::vector<RommRom> roms;					// every ROM of those platforms (for the saves)
		bool listed = false;
		bool scoped() const { return !onlyRom.empty() || onlyQueued; }
		std::vector<JPtr> romJson;
		// (the list as fetched, and the systems left out of it: each call's own view is made from it, so one skipping
		// more doesn't take games from one after it that skips less)
		std::map<long long, std::string> allPlatforms;
		std::vector<RommRom> allRoms;
		std::vector<JPtr> allJson;
		std::set<std::string> fetchSkip;
		bool list(const std::set<std::string>& skip, std::string& err)
		{
			// (listed once a run, and seen as each call skips: Get games' passes list every system, the saves leave out
			// those set to Skip. A call wanting a system the fetch left out fetches again)
			if (listed && std::includes(skip.begin(), skip.end(), fetchSkip.begin(), fetchSkip.end()))
			{
				platforms.clear(); roms.clear(); romJson.clear();
				for (auto& p : allPlatforms) if (!skip.count(p.second)) platforms.insert(p);
				for (auto& r : allRoms) if (!skip.count(r.folder)) roms.push_back(r);
				for (auto& j : allJson) if (platforms.count((long long)j->num("platform_id"))) romJson.push_back(j);
				return true;
			}
			listed = false;
			platforms.clear(); roms.clear(); romJson.clear();
			Resp r = call("GET", "/api/platforms");
			if (r.code != 200) { err = r.code == 401 || r.code == 403 ? "unpaired" : "platforms: " + r.error(); return false; }
			JPtr j = r.json();
			if (!j || !j->isArr()) { err = "the server's platforms can't be read"; return false; }
			for (auto& p : j->arr)
			{
				auto it = ROMM_SLUGS.find(p->str("slug"));
				std::string folder = it != ROMM_SLUGS.end() ? it->second : BANANA_FOLDERS.count(p->str("fs_slug")) ? p->str("fs_slug") : "";
				if (folder.empty() || skip.count(folder)) continue;
				// (one game's save: only its system's games listed, those in the folder its ROM is in)
				if (!onlyRom.empty() && sendOk)
				{
					std::string rf = romFolder(folder);
					if (rf.empty() || onlyRom.compare(0, rf.size() + 1, rf + "/") != 0) continue;
				}
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
			allPlatforms = platforms; allRoms = roms; allJson = romJson; fetchSkip = skip;
			return true;
		}
		bool haveList() const override { return listed && !scoped(); }
		bool manifest(Manifest& m, const std::set<std::string>& skip, std::string& err) override
		{
			if (!list(skip, err)) return false;
			std::map<std::string, MSystem> by;
			std::map<std::string, std::vector<std::pair<std::string, std::string>>> alt;	// per game as by has them: its regions, its file's stem
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
				g.title = romTitle(*it, it->str("fs_name_no_ext", stemOf(fsName)));
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
						if (packedName(fsName)) { f.packed = true; f.members = romFiles(*it)[0].members; }
					}
					else f.packed = packedName(fsName);
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
				g.gl.push_back({ "name", g.title });
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
				alt[folder].push_back({ romRegions(*it), it->str("fs_name_no_ext", stemOf(fsName)) });
			}
			// two games of a system by one title (the same game from two regions, or two dumps): the regions after it,
			// and where that still doesn't tell them apart, the file names
			for (auto& kv : by)
			{
				auto& games = kv.second.games;
				auto& a = alt[kv.first];
				for (int pass = 0; pass < 2; pass++)
				{
					std::map<std::string, int> n;
					for (auto& g : games) n[g.title]++;
					for (size_t i = 0; i < games.size(); i++)
					{
						if (n[games[i].title] < 2) continue;
						if (pass == 0) { if (!a[i].first.empty()) games[i].title += " (" + a[i].first + ")"; }
						else games[i].title = a[i].second;
					}
				}
				for (auto& g : games) for (auto& kvp : g.gl) if (kvp.first == "name") kvp.second = g.title;
			}
			for (auto& kv : by) m.systems.push_back(kv.second);
			return true;
		}
		// A game's save is the one file its emulator reads (save_paths.h): saves/nds/<stem>.sav, saves/gba/<stem>.srm,
		// saves/<stem>.srm, for a game on the card whose ROM is the server's by its contents. On RomM 5.4
		// and newer it syncs with that ROM's autosave slot (slots() below). An older RomM's saves (one per ROM and file
		// name, no slots) only come down (sync() below, which the states use too): changed on the server only ->
		// download; changed on both -> the server's taken and the card's kept beside it as <file>.card-<date>.bak. A
		// blank save here (a core's fresh one) is never sent: it takes the server's when it was never synced, and is
		// left alone when it was (blanked since). Every download is checked against the server's MD5 and installed
		// through the vault.
		struct Canon { RommRom rom; std::string romRel, label; };
		std::map<std::string, Canon> canonical;		// data-relative save path -> its game
		std::map<long long, std::string> saveOf;	// ROM id -> its save's path
		std::set<long long> unsure;	// games on the card with a save in play that can't be said for sure (not in canonical)
		std::map<std::string, std::map<std::string, std::string>> cardIndex;	// system -> its ROMs on the card
		// the card's copy of a server ROM, by name only (roms-relative; "" when it has none)
		std::string onCard(const RommRom& r)
		{
			if (r.entry.empty()) return "";
			if (!cardIndex.count(r.folder))
			{
				std::string f = romFolder(r.folder);
				cardIndex[r.folder] = f.empty() ? std::map<std::string, std::string>() : romIndex(f);
			}
			auto li = cardIndex[r.folder].find(r.entry);
			return li == cardIndex[r.folder].end() ? "" : li->second;
		}
		// a game whose saves stay on this handheld (its page's Save sync), keyed as that page writes it: the folder on the
		// card, which systems.conf can name apart from the system, and the ROM's file name
		bool keptHere(const RommRom& r, const std::string& here) const { return local.count(romFolder(r.folder) + "/" + baseOf(here)) > 0; }
		// each game on the card with a save in play (one on the server, or one here in any folder under saves/), and
		// its save's path. What can't be said for sure is said instead: a system whose save folder isn't known, a ROM
		// other than the server's
		void resolve(const std::set<long long>& onServer, const std::set<std::string>& stemsHere, std::vector<std::string>& errors, std::string& note)
		{
			canonical.clear(); saveOf.clear(); unsure.clear();
			std::map<std::string, SysSaves> sys;
			std::set<std::string> told, shared;
			int unmatched = 0;
			for (auto& r : roms)
			{
				std::string here = onCard(r);
				if (here.empty()) continue;	// (not on this card)
				if (keptHere(r, here)) { unsure.insert(r.id); say("  not synced, its saves stay on this handheld: %s", here.c_str()); continue; }
				if (here == resumeRom && onlyRom != here) { unsure.insert(r.id); say("  not synced, about to start again after Power off after sleeping, its resume state with the save here: %s", here.c_str()); continue; }
				if (!sys.count(r.folder)) sys[r.folder] = systemSaves(r.folder);
				const SysSaves& ss = sys[r.folder];
				std::string stem = stemOf(baseOf(here));
				if (!ss.known())
				{
					if (!onServer.count(r.id) && !stemsHere.count(stem)) continue;
					unsure.insert(r.id);
					if (told.insert(r.folder).second) errors.push_back("saves: " + ss.err);
					continue;
				}
				std::string rel = ss.rel(stem);
				if (!onServer.count(r.id) && !stemsHere.count(stem)) continue;	// (no save in play: not hashed)
				std::string ndsWhy = ndsDivergedWhy(rel);
				if (!ndsWhy.empty())
				{
					unsure.insert(r.id);
					say("  not synced, %s: %s", ndsWhyLog(ndsWhy), here.c_str());
					// (a note for the Saves page, with nothing to choose; gone once the two agree again)
					SlotNote nt{}; nt.rel = rel; nt.why = ndsWhy; nt.rom = here;
					nt.localMd5 = hashFile(ROOT + "/" + rel, "md5");
					c.held[std::to_string(r.id) + "/" + SLOT] = nt;
					continue;
				}
				if (!romMatches(here, r.files, c))
				{
					unmatched++;
					unsure.insert(r.id);
					say("  not synced, not the server's ROM (other bytes): %s", here.c_str());
					continue;
				}
				if (canonical.count(rel)) { shared.insert(rel); unsure.insert(r.id); continue; }
				canonical[rel] = { r, here, ss.label };
				saveOf[r.id] = rel;
			}
			for (auto& rel : shared)
			{
				say("  not synced, two games' save: %s", rel.c_str());
				unsure.insert(canonical[rel].rom.id);
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
		bool checkVersion(std::string& err)
		{
			Resp r = call("GET", "/api/heartbeat");
			if (r.code == 0) { err = r.error(); return false; }	// (no answer: the next request would only wait as long)
			JPtr j = r.code == 200 ? r.json() : nullptr;
			const JVal* sys = j ? j->get("SYSTEM") : nullptr;
			if (sys && !sys->str("VERSION").empty() && sys->str("VERSION") != c.serverVersion)
			{
				c.serverVersion = sys->str("VERSION");
				c.serverName = "RomM " + c.serverVersion;	// (as pairing names it)
				saveConf(c);
			}
			sendOk = c.serverVersion.empty() || versionAtLeast(c.serverVersion, 5, 4, 0);
			return true;
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
			// (the version first: what is listed for one game's save depends on it)
			if (!checkVersion(err)) { errors.push_back("saves: " + err); keepUnsent(errors); return false; }
			if (beforeGame) status("answered");	// (the menu's wait for a first answer is over: the transfer has its own cap)
			oneGame = sendOk && !onlyRom.empty();
			if (!sendOk) addNote(note, "RomM " + c.serverVersion + " only gives saves: update RomM to 5.4 or newer to send this handheld's");
			// (a RomM before 5.4 has no slots to sync one game's by: its saves all come down, as they did after a game, but
			// not while a game waits to start, and not for saves it could never take; nothing more is asked of it then)
			if (!sendOk && (beforeGame || onlyQueued)) return true;
			// (a path still absolute: on neither card's ROMs folder, an app's. Slots know only the games under those)
			if (sendOk && !onlyRom.empty() && onlyRom[0] == '/') return true;
			if (!list(skip, err)) { errors.push_back("saves: " + err); keepUnsent(errors); return false; }
			if (!checkDevice(note)) return true;
			if (!onlyRom.empty() && !sendOk) say("RomM %s: every save synced, not only %s's", c.serverVersion.c_str(), onlyRom.c_str());
			if (scoped() && sendOk)
			{
				// (one game's save, or those waiting to be sent: the server's ROMs that are those games on the card, the only
				// ones looked at below)
				std::vector<RommRom> some;
				for (auto& r : roms)
				{
					std::string key = std::to_string(r.id) + "/" + SLOT, here = onlyQueued ? onCard(r) : "";
					// (one waiting under its ROM, never synced: under its slot from here, decided as any save never agreed)
					auto qr = here.empty() ? c.queue.end() : c.queue.find("rom:" + here);
					if (qr != c.queue.end())
					{
						if (!c.queue.count(key)) { QueueSt w = qr->second; w.rel.clear(); c.queue[key] = w; }
						c.queue.erase(qr);
					}
					if (onlyQueued ? c.queue.count(key) > 0 : onCard(r) == onlyRom) some.push_back(r);
				}
				if (some.empty()) { if (!onlyRom.empty()) addNote(note, "not a game on the server: " + onlyRom); return true; }
				roms = some;
				for (auto& r : roms) if (r.folder == "nds" && !onCard(r).empty()) ndsSaves(romPath(onCard(r)));
			}
			else ndsSavesFirst();
			auto local = localSaves(statesToo);
			bool ok = sendOk ? slots(local, up, down, conflicts, errors, note) : sync("save", local, c.saves, up, down, conflicts, errors, note);
			if (statesToo) ok &= sync("state", local, c.states, up, down, conflicts, errors, note);
			return ok;
		}
		// the game's save kept, the server out of reach, and left waiting to be sent, which the sync when Wi-Fi comes back
		// sends: under its slot when this card has synced it before, else under its ROM
		void keepUnsent(std::vector<std::string>& errors)
		{
			Played g;
			if (!keepPlayed(g, errors)) return;
			std::string key;
			for (auto& s : c.slots) if (s.second.rel == g.rel) key = s.first;
			if (g.ndsHeld || (!key.empty() && c.slots[key].md5 == g.md5)) { say("  kept in the vault, the server out of reach: %s", g.rel.c_str()); return; }
			// (a game never synced from this card: its slot isn't known until the server is reached, so it waits under its
			// ROM, which the sync when Wi-Fi comes back looks up)
			if (key.empty()) key = "rom:" + onlyRom;
			QueueSt& w = c.queue[key];
			if (w.md5 != g.md5) w = { g.md5, g.vault, (double)time(nullptr), key.compare(0, 4, "rom:") == 0 ? g.rel : "" };
			say("  waiting to be sent, the server out of reach (a copy in the vault): %s", g.rel.c_str());
			keptUnsent = true;
			saveConf(c);
		}
		// ---- RomM's save slots (5.4 and newer): each game's save is the autosave slot of its ROM, a version per change,
		// shared with the other devices that play it. Which way a save goes is decided here by content alone, from three
		// MD5s: L, the card's save; S, the slot's head (its newest version by updated_at then id, the order RomM's own
		// upload guard keeps); A, the version both sides last held (romsync.json's slots). Only one side moved from A:
		// that side's goes to the other. Both moved, or the card never agreed with the slot and holds other bytes: a
		// conflict, held, both kept (the server's fetched into the vault), until its owner says which. No clock is ever
		// compared, RomM's negotiation plan is never followed (it goes by clocks, and of versions saved in the same
		// second it may offer any), and nothing is deleted or overwritten on the server: no overwrite, no cleanup.
		const std::string SLOT = "autosave";
		struct Head { long long id = 0, size = -1; time_t t = 0; std::string name, hash, updated, device; bool current = false; };
		std::vector<long long> sessions;	// negotiation's, completed at the end of the saves
		std::map<std::string, SlotNote> prior, priorHeld;	// what was held before this run (a conflict's vault copy kept)
		std::set<std::string> open;	// the games this run couldn't decide: what they had held or waiting is kept
		// the head is the version the slot last agreed, by MD5 alone (the id stays when a row is written over, so it says
		// nothing of the bytes; a head with no MD5 is never it). Never for a save lost since: what is here now is a new save
		bool agreedHead(const std::string& key, const std::string& rel, const Head& h) const
		{
			auto si = c.slots.find(key);
			if (si == c.slots.end() || si->second.rel != rel || si->second.lost || h.hash.empty()) return false;
			const SlotSt& s = si->second;
			return h.hash == s.md5 || (!s.wire.empty() && h.hash == s.wire);
		}
		// the slot's version both sides now hold (whatever was waiting to be sent for it is done with; wire: the server's
		// MD5 for it when the card holds it in another form). A save that waited and is another version than this one
		// never reached the server (the server's taken over it, the card's gone): its copy in the vault is the only one
		// anywhere, said, as nothing names it once it no longer waits (nothing removes a copy from the vault)
		void agree(const std::string& key, const std::string& md5, const std::string& rel, long long id, const std::string& wire = "")
		{
			c.slots[key] = { md5, rel, id, wire };
			auto qi = c.queue.find(key);
			if (qi == c.queue.end()) return;
			if (qi->second.md5 != md5 && !qi->second.vault.empty() && !dryRun) say("  never sent, kept in the vault: %s", qi->second.vault.c_str());
			c.queue.erase(qi);
		}
		enum class Up { Sent, Same, Held, Format, Failed };
		// a head this handheld can decide on: its MD5 (asked of the server when the listing has none), a file its system's
		// saves come in and no larger than they are. Held (recorded and said) when it isn't; Error (said) when its MD5
		// can't be had
		enum class Look { Ok, Held, Error };
		Look look(Head& h, const Canon& g, const std::string& rel, const std::string& key, const std::string& L, std::vector<std::string>& errors)
		{
			long long max = maxSave(g.rom.folder);
			if (max >= 0 && h.size > max) { hold(rel, key, L, h, "larger than any save of its system", "", false, errors); return Look::Held; }
			if (h.hash.empty()) h.hash = serverHash("/api/saves", h);
			// (one with no checksum is held all the same, its copy not fetched: it couldn't be checked)
			if (!wireOk(g.rom.folder, h.name, extOf(rel))) { hold(rel, key, L, h, "a format this handheld doesn't read", "", !h.hash.empty(), errors); return Look::Held; }
			if (h.hash.empty()) { errors.push_back(baseOf(rel) + ": the server gives no checksum for it, not taken"); return Look::Error; }
			return Look::Ok;
		}
		// the vault copy of this head recorded for the game before this run, held or a conflict, while it is still there
		// and still the head's bytes by the server's MD5 (the vault is the owner's to look in, and a copy changed there is
		// fetched again, never taken as the server's)
		std::string keptBefore(const std::string& key, const Head& h)
		{
			if (h.hash.empty()) return "";
			for (auto* from : { &prior, &priorHeld })
			{
				auto b = from->find(key);
				if (b != from->end() && b->second.saveId == h.id && !b->second.vault.empty() && exists(b->second.vault) && hashFile(b->second.vault, "md5") == h.hash)
					return b->second.vault;
			}
			return "";
		}
		// the head fetched to dl (on /run) and checked against its MD5; false (said, dl removed) when it can't be
		bool fetchHead(const std::string& rel, const Head& h, const std::string& dl, std::vector<std::string>& errors)
		{
			std::string e;
			if (fetchTo(contentUrl(h), auth(), dl, -1, "md5", h.hash, e)) return true;
			dropDownload(dl);
			errors.push_back(baseOf(rel) + ": " + e);
			return false;
		}
		// a head this handheld won't take by itself, held: recorded with why, the server's copy in the vault for its owner
		// to see (from dl when it has been fetched, else fetched here unless fetch is false: too large, or no checksum to
		// check it by; kept from the last run while the head is the same). dl is removed
		std::string romOf(const std::string& rel) const { auto i = canonical.find(rel); return i == canonical.end() ? "" : i->second.romRel; }
		void hold(const std::string& rel, const std::string& key, const std::string& L, const Head& h, const std::string& why, std::string dl, bool fetch, std::vector<std::string>& errors)
		{
			SlotNote nt{ rel, why, L, h.hash, h.device, h.updated, keptBefore(key, h), h.id, romOf(rel) };
			say("  not taken yet, %s: %s", why.c_str(), h.name.c_str());
			if (nt.vault.empty() && !dryRun)
			{
				if (dl.empty() && fetch)
				{
					dl = RUN + "/sync-dl." + std::to_string(getpid());
					if (!fetchHead(rel, h, dl, errors)) dl.clear();
				}
				std::string e;
				if (!dl.empty() && !vaultKeep(rel, dl, nt.vault, e)) errors.push_back(baseOf(rel) + ": the server's copy can't be kept in the vault: " + e);
			}
			if (!dl.empty()) unlink(dl.c_str());
			c.held[key] = nt;
		}
		static time_t isoTime(const std::string& s)
		{
			struct tm tmv = {};
			return strptime(s.substr(0, 19).c_str(), "%Y-%m-%dT%H:%M:%S", &tmv) ? timegm(&tmv) : 0;
		}
		std::string devQ() const { return "device_id=" + urlEsc(c.device); }
		// which of these ROMs have a save in the slot: RomM's negotiation (one call per 500, restore_unlisted so a head
		// this device synced before is reported too), its plan only logged
		bool negotiate(const std::vector<long long>& ids, std::set<long long>& with, std::string& err)
		{
			std::map<std::string, int> plan;
			for (size_t a = 0; a < ids.size(); a += 500)
			{
				std::set<long long> scope(ids.begin() + a, ids.begin() + std::min(ids.size(), a + 500));
				std::string list;
				for (long long id : scope) list += (list.empty() ? "" : ", ") + std::to_string(id);
				std::string body = "{\"device_id\": " + jsonStr(c.device) + ", \"rom_ids\": [" + list + "], \"saves\": [], \"restore_unlisted\": true}";
				Resp r = call("POST", "/api/sync/negotiate", &body);
				if (r.code != 200) { err = "negotiation: " + r.error(); return false; }
				JPtr j = r.json();
				const JVal* ops = j && j->isObj() ? j->get("operations") : nullptr;
				if (!ops || !ops->isArr() || !j->get("session_id") || !j->get("session_id")->isNum()) { err = "RomM's negotiation can't be read"; return false; }
				sessions.push_back((long long)j->num("session_id"));
				for (auto& o : ops->arr)
				{
					if (!scope.count((long long)o->num("rom_id"))) { err = "RomM's negotiation answered for a game it wasn't asked about"; return false; }
					plan[o->str("action")]++;
					if (o->str("slot") == SLOT) with.insert((long long)o->num("rom_id"));
				}
			}
			std::string p;
			for (auto& kv : plan) p += (p.empty() ? "" : ", ") + std::to_string(kv.second) + " " + kv.first;
			if (!p.empty()) say("  negotiation's own plan: %s (not followed)", p.c_str());
			return true;
		}
		// each of these ROMs' slot head, from the slot's versions as RomM lists them (in no order)
		bool listHeads(const std::vector<long long>& ids, std::map<long long, Head>& heads, std::string& err)
		{
			for (size_t a = 0; a < ids.size(); a += 100)
			{
				std::set<long long> scope(ids.begin() + a, ids.begin() + std::min(ids.size(), a + 100));
				std::string q = "/api/saves?slot=" + SLOT + (c.device.empty() ? "" : "&" + devQ());
				for (long long id : scope) q += "&rom_ids=" + std::to_string(id);
				Resp r = call("GET", q);
				if (r.code != 200) { err = "saves: " + r.error(); return false; }
				JPtr j = r.json();
				if (!j || !j->isArr()) { err = "the server's saves can't be read"; return false; }
				for (auto& s : j->arr)
				{
					long long rid = (long long)s->num("rom_id");
					if (!scope.count(rid)) { err = "the server listed a save of a game it wasn't asked about"; return false; }
					if (s->str("slot") != SLOT || s->flag("missing_from_fs")) continue;
					Head h;
					h.id = (long long)s->num("id"); h.updated = s->str("updated_at"); h.t = isoTime(h.updated);
					if (s->get("file_size_bytes")) h.size = (long long)s->num("file_size_bytes");
					h.name = s->str("file_name"); h.hash = lower(s->str("content_hash"));
					// (the device it came from by the name its sync record has: the id alone says nothing to a person, and
					// with no name listed none is shown)
					std::string origin;
					if (const JVal* o = s->get("origin_device_id")) origin = o->isNum() ? fmtNum(o->n) : o->s;
					if (const JVal* ds = s->get("device_syncs"))
						if (ds->isArr())
							for (auto& d : ds->arr)
							{
								const JVal* di = d->get("device_id");
								std::string id = !di ? "" : di->isNum() ? fmtNum(di->n) : di->s;
								if (!id.empty() && id == c.device) h.current = d->flag("is_current");
								if (!id.empty() && id == origin) h.device = d->str("device_name");
							}
					Head& e = heads[rid];
					if (!e.id || h.t > e.t || (h.t == e.t && h.id > e.id)) e = h;
				}
			}
			return true;
		}
		// the slot's head seen by this device, so RomM's guard lets its next upload in (only for a version this card
		// holds, or agreed, or a blank one)
		bool confirm(const Head& h)
		{
			std::string b = "{\"device_id\": " + jsonStr(c.device) + ", \"content_hash\": " + jsonStr(h.hash) + "}";
			Resp r = call("POST", "/api/saves/" + std::to_string(h.id) + "/downloaded", &b);
			return r.code == 200;
		}
		std::string contentUrl(const Head& h) const
		{
			return c.server + "/api/saves/" + std::to_string(h.id) + "/content?optimistic=false" + (c.device.empty() ? "" : "&" + devQ());
		}
		bool slots(const std::map<std::string, std::string>& localAll, int& up, int& down, int& conflicts, std::vector<std::string>& errors, std::string& note)
		{
			// the server ROMs this card has (by name), those with a slot head, and the heads
			std::vector<long long> cand;
			for (auto& r : roms) if (!onCard(r).empty()) cand.push_back(r.id);
			std::set<long long> with(cand.begin(), cand.end());
			std::map<long long, Head> heads;
			std::string err;
			size_t errs0 = errors.size();
			sessions.clear();
			// (no device: no negotiation, which needs one; a dry run: none either, it opens a session)
			bool ok = c.device.empty() || dryRun || (with.clear(), negotiate(cand, with, err));
			ok = ok && listHeads(std::vector<long long>(with.begin(), with.end()), heads, err);
			if (!ok) { errors.push_back("saves: " + err); keepUnsent(errors); complete(0, 1); return false; }
			std::set<long long> onServer;
			for (auto& h : heads) onServer.insert(h.first);
			std::set<std::string> stemsHere;
			for (auto& kv : localAll) if (kv.second == "save") stemsHere.insert(stemOf(baseOf(kv.first)));
			resolve(onServer, stemsHere, errors, note);
			for (auto& kv : canonical) keepOthers(kv.first, errors);
			if (!dryRun && c.stateVersion < 2)
			{
				// (the file as banana-sync 1.0 left it, kept once beside it. Its saves{} gives no agreed version: a slot whose
				// head is the card's very save is agreed by the first rule below, and the card's against other bytes is a
				// conflict, whatever 1.0 last sent or took)
				std::string v1 = dirOf(CONF) + "/romsync.v1.json";
				bool had;
				std::string old = readFile(CONF, &had);
				if (had && !exists(v1)) writeAtomic(v1, old, 0600);
				c.stateVersion = 2;
			}
			// (what is held back is worked out again for each game decided this run; one that isn't, an error on the way or
			// its save folder not known, keeps what it had, and a conflict's vault copy is kept from the last)
			prior = c.conflicts; priorHeld = c.held;
			open.clear();
			auto undecided = [&](const std::string& key)
			{
				open.insert(key);
				if (prior.count(key)) c.conflicts[key] = prior[key];
				if (priorHeld.count(key)) c.held[key] = priorHeld[key];
			};
			int n = 0, total = (int)canonical.size(), done = 0;
			bool waiting = false;
			for (auto& kv : canonical)
			{
				const std::string& rel = kv.first;
				const Canon& g = kv.second;
				n++;
				status("running " + std::to_string(n) + " " + std::to_string(total) + " Saves|" + baseOf(rel));
				std::string key = std::to_string(g.rom.id) + "/" + SLOT, p = ROOT + "/" + rel;
				// (one that waited under its ROM, never synced: under its slot now, whatever this run makes of it)
				auto qr = c.queue.find("rom:" + g.romRel);
				if (qr != c.queue.end()) { if (!c.queue.count(key)) { QueueSt w = qr->second; w.rel.clear(); c.queue[key] = w; } c.queue.erase(qr); }
				if (stopForGame(errors)) { undecided(key); continue; }
				c.conflicts.erase(key); c.held.erase(key);
				auto hi = heads.find(g.rom.id);
				Head* h = hi == heads.end() ? nullptr : &hi->second;
				bool here = localAll.count(rel) > 0, readOk = true;
				// (read once: the MD5 is of the bytes judged, not of a second reading)
				std::string bytes = here ? readFile(p, &readOk) : "", L = here && readOk ? hashStr(bytes, "md5") : "";
				if (here && (L.empty() || !readOk)) { errors.push_back(rel + ": can't be read"); undecided(key); continue; }
				bool real = here && !blankSave(bytes, saveFolder(rel));
				auto si = c.slots.find(key);
				std::string A = si != c.slots.end() && si->second.rel == rel ? si->second.md5 : "";
				// (a save gone missing or blank since it was agreed: what is at its path next is a new save, not the agreed
				// one played on, so it is decided as never agreed: the server's very bytes agree, others are a conflict, both
				// kept, never a version sent over the server's as the newer. The record stays, marked, until a version is
				// agreed, so the server's isn't put back by itself and a conflict over it doesn't fall away at the next run)
				bool lost = real && !A.empty() && si->second.lost;
				if (lost) A.clear();
				Look lk = h ? look(*h, g, rel, key, L, errors) : Look::Ok;
				if (lk == Look::Held) continue;
				if (lk == Look::Error) { undecided(key); continue; }
				auto send = [&]()
				{
					Up u = upload(g, rel, key, L, h, errors);
					if (u == Up::Sent) { up++; done++; } else if (u == Up::Held) conflicts++; else if (u == Up::Failed) undecided(key);
				};
				std::string S = h ? h->hash : "";
				// (the head as the card took it in its own form, a .dsv as its raw save: the agreed version still)
				std::string W = si != c.slots.end() && si->second.rel == rel && !lost ? si->second.wire : "";
				bool viaWire = !W.empty() && S == W;
				if (viaWire) S = A;
				if (real && L == S)
				{
					// (agreed: its last good copy in the vault, as a save sent or taken has, when the agreement is new)
					if (L != A && !dryRun) vaultGood(rel, bytes);
					agree(key, L, rel, h->id, viaWire ? W : "");
					if (!h->current && !c.device.empty() && !dryRun) confirm(*h);
					continue;
				}
				if (!real && !A.empty())
				{
					// (the emulator's data cleared, perhaps on purpose: never refilled by itself. A save of the game's that
					// waited to be sent still waits, its copy in the vault named here, so what was played isn't lost with it)
					std::string why = here ? NOTE_BLANK : NOTE_MISSING;
					si->second.lost = true;
					auto qi = c.queue.find(key);
					// (the server's copy in the vault too, once, so its owner can take it back; with none on the server a save
					// that waited names its own copy, which the page offers as a kept one)
					if (h) hold(rel, key, L, *h, why, "", true, errors);
					else c.held[key] = { rel, why, L, "", "", "", qi == c.queue.end() ? "" : qi->second.vault, 0, g.romRel };
					if (qi != c.queue.end()) open.insert(key);
					// (counted with the conflicts when the server's is there to take back: the menu asks before a game is
					// played from nothing over it)
					if (h && !c.held[key].vault.empty()) conflicts++;
					say("  %s, left as it is: %s", why.c_str(), rel.c_str());
					continue;
				}
				if (!real) { if (h && download(g, rel, key, L, *h, errors)) { down++; done++; } continue; }
				// its owner chose the card's over the version they saw: sent as any change (the upload marks that head seen,
				// which lets it go up with no overwrite asked), and a head that has moved since is held again as what it now is
				if (h && key == settleKey && prior.count(key) && prior[key].serverMd5 == h->hash && !c.device.empty()) { send(); continue; }
				if (L == A && !h) continue;	// (gone from the server, deleted there: not sent back until it changes here)
				if (!A.empty() && L == A)
				{
					// (the mark the menu asks by, before a game plays over a newer save it couldn't have: there from the start,
					// so a run stopped at the cap leaves it too)
					size_t before = errors.size();
					if (beforeGame && !dryRun) writeAtomic(RUN + "/launch.older", "");
					bool got = download(g, rel, key, L, *h, errors);
					if (got) { down++; done++; }
					if (beforeGame && (got || errors.size() == before)) unlink((RUN + "/launch.older").c_str());
					continue;
				}
				if (!h || (!A.empty() && S == A))
				{
					if (c.device.empty()) { waiting = true; continue; }
					send();
					continue;
				}
				// both moved, or never agreed: held, unless the server's is a fresh blank save (no save at all)
				Both r = conflict(g, rel, key, L, *h, errors);
				if (r == Both::Held) conflicts++;
				else if (r == Both::Blank) { if (c.device.empty()) waiting = true; else send(); }
				else if (r == Both::Same) agreeSame(key, L, rel, *h);
				else if (r == Both::Failed) undecided(key);
			}
			// (what was held for a game this run didn't look at stays only while the game can't be said for sure: one gone
			// from the card, or with no save in play any more, has its records dropped, its vault copies kept. A save waits
			// only while its game is undecided: one decided, or with no save in play, has nothing left to send, and one
			// that can't be said for sure is sent by the sync that can, from the card)
			std::set<std::string> kept;
			// (one game's sync looks at that game alone: every other's records stay as they are)
			for (auto& kv : canonical) kept.insert(std::to_string(kv.second.rom.id) + "/" + SLOT);
			for (long long id : unsure) kept.insert(std::to_string(id) + "/" + SLOT);
			std::set<std::string> scope;
			for (auto& r : roms) scope.insert(std::to_string(r.id) + "/" + SLOT);
			// (a state's record, "<rom id>/state:<name>", is the states' own: left as it is here)
			auto slotKey = [&](const std::string& key) { return key.size() > SLOT.size() && key.compare(key.size() - SLOT.size() - 1, std::string::npos, "/" + SLOT) == 0; };
			auto stays = [&](const std::string& key, bool keep) { return keep || !slotKey(key) || (scoped() && !scope.count(key)); };
			for (auto* m : { &c.conflicts, &c.held })
				for (auto it = m->begin(); it != m->end();) it = stays(it->first, kept.count(it->first)) ? std::next(it) : m->erase(it);
			for (auto it = c.queue.begin(); it != c.queue.end();)
			{
				if (stays(it->first, open.count(it->first))) { ++it; continue; }
				// (said: a copy of what was played stays in the vault, and nothing else will mention it)
				if (!it->second.vault.empty()) say("  no longer to be sent, its game gone from the card or its save no longer in play (the copy stays in the vault): %s", it->second.vault.c_str());
				it = c.queue.erase(it);
			}
			size_t heldSaves = 0;
			for (auto& kv : c.held) if (slotKey(kv.first)) heldSaves++;
			if (heldSaves) addNote(note, std::to_string(heldSaves) + (heldSaves == 1 ? " save" : " saves") + " held back (Manage saves says why)");
			if (waiting) addNote(note, "this handheld's saves are sent once it is a RomM device");
			if (!c.queue.empty()) addNote(note, std::to_string(c.queue.size()) + (c.queue.size() == 1 ? " save" : " saves") + " waiting to be sent");
			complete(done, (int)(errors.size() - errs0));
			saveConf(c);
			return true;
		}
		// the card's save and the slot's head both real and other bytes: Held, a conflict, the server's head fetched into
		// the vault (unless it is there from the last run) and recorded. Not when the head, as this handheld would take it,
		// is blank (Blank: no save, the card's may go) or the card's very save in another form (Same: a .dsv's raw save);
		// NotTaken when this handheld can't take it at all (held, said); Failed (said)
		enum class Both { Held, Blank, Same, NotTaken, Failed };
		Both conflict(const Canon& g, const std::string& rel, const std::string& key, const std::string& L, const Head& h, std::vector<std::string>& errors)
		{
			if (dryRun) { say("  conflict %s", rel.c_str()); return Both::Held; }
			SlotNote nt{ rel, "", L, h.hash, h.device, h.updated, keptBefore(key, h), h.id, g.romRel };
			std::string dl = nt.vault.empty() ? RUN + "/sync-dl." + std::to_string(getpid()) : "", e;
			if (!dl.empty() && !fetchHead(rel, h, dl, errors)) return Both::Failed;
			bool ok;
			std::string data = readFile(dl.empty() ? nt.vault : dl, &ok), local = readFile(ROOT + "/" + rel);
			if (!ok) { if (!dl.empty()) unlink(dl.c_str()); errors.push_back(baseOf(rel) + ": the server's copy can't be read"); return Both::Failed; }
			std::string why = adaptIn(g.rom.folder, data, &local);
			if (blankSave(data, saveFolder(rel))) { if (!dl.empty()) unlink(dl.c_str()); return Both::Blank; }
			if (why.empty() && hashStr(data, "md5") == L) { if (!dl.empty()) unlink(dl.c_str()); return Both::Same; }
			if (!why.empty())
			{
				hold(rel, key, L, h, why, dl, false, errors);
				return Both::NotTaken;
			}
			if (!dl.empty())
			{
				bool kept = vaultKeep(rel, dl, nt.vault, e);
				unlink(dl.c_str());
				if (!kept) { errors.push_back(baseOf(rel) + ": the server's copy can't be kept in the vault: " + e); return Both::Failed; }
			}
			c.conflicts[key] = nt;
			c.queue.erase(key);	// (not sent by itself any more: its owner decides)
			say("  conflict, both kept: %s (the server's in the vault)", rel.c_str());
			return Both::Held;
		}
		// the head is the card's save in another form: agreed, the server's MD5 for it kept beside the card's, and the
		// head marked seen
		void agreeSame(const std::string& key, const std::string& L, const std::string& rel, const Head& h)
		{
			agree(key, L, rel, h.id, h.hash);
			if (!h.current && !c.device.empty()) confirm(h);
			say("  the server's newest is this same save in another form: %s", rel.c_str());
		}
		// the card's save as the slot's next version (h: the head it goes over, confirmed first when this device hasn't
		// seen it: the agreed version, or a blank one). Refused (409, a version came in since the listing): the slot
		// looked at again, agreed when that version holds the same bytes, else held; never sent over it. No answer, or
		// the server's error: waiting to be sent, its bytes in the vault
		Up upload(const Canon& g, const std::string& rel, const std::string& key, const std::string& L, const Head* h, std::vector<std::string>& errors)
		{
			if (dryRun) { say("  upload %s", rel.c_str()); return Up::Sent; }
			std::string p = ROOT + "/" + rel;
			// a copy of the file as it is now: the emulator can't change what is sent halfway
			std::string snap = RUN + "/sync-upload." + std::to_string(getpid());
			std::string body = readFile(p);
			if (hashStr(body, "md5") != L || !writeAtomic(snap, body)) { errors.push_back(rel + ": changed while syncing, next time"); unlink(snap.c_str()); return Up::Failed; }
			// (a file of a size its system's saves don't come in, as one cut short by a power cut while the emulator
			// wrote it: not sent, said, as the other handhelds would refuse it anyway)
			std::string whole = body, why = adaptIn(g.rom.folder, whole, nullptr);
			if (!why.empty()) { errors.push_back(rel + ": " + why + ", not sent"); unlink(snap.c_str()); return Up::Failed; }
			if (h && !h->current && !confirm(*h)) { errors.push_back(rel + ": the server's newest version can't be marked seen"); unlink(snap.c_str()); return Up::Failed; }
			Req q; q.method = "POST"; q.headers = auth(); q.timeout = 0;
			q.url = c.server + "/api/saves?rom_id=" + std::to_string(g.rom.id) + "&slot=" + SLOT + "&" + devQ() + "&emulator=" + urlEsc(g.label) + "&content_hash=" + L;
			q.fileParts.push_back({ "saveFile", snap });
			q.partName = baseOf(rel);
			std::map<long long, Head> now;	// the slot looked at again after a refusal, its head's MD5 asked for when not listed
			auto relist = [&]
			{
				std::string e;
				now.clear();
				if (listHeads({ g.rom.id }, now, e) && now.count(g.rom.id))
				{
					Head& nh = now[g.rom.id];
					if (nh.hash.empty()) nh.hash = serverHash("/api/saves", nh);
					return true;
				}
				unlink(snap.c_str());
				errors.push_back(rel + ": changed on the server meanwhile, next time");
				return false;
			};
			Resp r = http(q);
			if (r.code == 409)
			{
				if (!relist()) return Up::Failed;
				// (the head still the agreed version: RomM refused for want of this device's record of it, which its
				// listing said it had. Marked seen and sent once more; refused again, it is held as any other)
				Head& nh = now[g.rom.id];
				if (agreedHead(key, rel, nh) && confirm(nh))
				{
					say("  refused over the agreed version, marked seen and sent again: %s", rel.c_str());
					r = http(q);
					if (r.code == 409 && !relist()) return Up::Failed;
				}
			}
			JPtr j = r.code == 409 ? nullptr : r.json();
			if (r.code != 409 && ((r.code != 200 && r.code != 201) || !j || !j->isObj()))
			{
				errors.push_back(rel + ": " + r.error());
				QueueSt& w = c.queue[key];
				std::string kept, e;
				if (w.md5 != L) w = { L, vaultKeep(rel, snap, kept, e) ? kept : "", (double)time(nullptr) };
				unlink(snap.c_str());
				saveConf(c);
				return Up::Failed;
			}
			unlink(snap.c_str());
			if (r.code == 409)
			{
				Head& nh = now[g.rom.id];
				Look lk = look(nh, g, rel, key, L, errors);
				if (lk == Look::Held) return Up::Format;
				if (lk == Look::Error) return Up::Failed;
				if (nh.hash == L)
				{
					agree(key, L, rel, nh.id);
					if (!nh.current) confirm(nh);
					say("  the server's newest is this same save: %s", rel.c_str());
					saveConf(c);
					return Up::Same;
				}
				Both r = conflict(g, rel, key, L, nh, errors);
				if (r == Both::Same) { agreeSame(key, L, rel, nh); saveConf(c); return Up::Same; }
				if (r == Both::NotTaken) return Up::Format;
				if (r == Both::Held) { saveConf(c); return Up::Held; }
				errors.push_back(rel + ": changed on the server meanwhile, next time");
				return Up::Failed;
			}
			agree(key, L, rel, (long long)j->num("id"));
			vaultGood(rel, body);
			say("  up %s", rel.c_str());
			saveConf(c);
			return Up::Sent;
		}
		// the slot's head as the card's save: fetched to /run and checked, the card's kept in the vault, installed, then
		// told to RomM as this device's (L: the card's save as it was decided on, "" for none)
		bool download(const Canon& g, const std::string& rel, const std::string& key, const std::string& L, const Head& h, std::vector<std::string>& errors)
		{
			std::string p = ROOT + "/" + rel;
			if (dryRun) { say("  download %s", p.c_str()); return false; }
			if (exists(p) ? hashFile(p, "md5") != L : !L.empty()) { errors.push_back(baseOf(p) + ": changed while syncing, next time"); return false; }
			// (a head held before, fetched once: its copy in the vault read instead while it is still the slot's newest)
			std::string dl = RUN + "/sync-dl." + std::to_string(getpid()), err, kept = keptBefore(key, h);
			if (kept.empty() && !fetchHead(rel, h, dl, errors)) return false;
			// (as this handheld's emulator reads it: a .dsv's footer cut off; checked against the system's sizes and the
			// card's save. A blank one there says nothing of the game's size: a fresh core writes its own whole banks,
			// so it is replaced, kept in the vault, as any other)
			bool ok, had = exists(p);
			std::string data = readFile(kept.empty() ? dl : kept, &ok), local = had ? readFile(p) : "";
			bool sized = had && !blankSave(local, saveFolder(rel));
			std::string why = ok ? adaptIn(g.rom.folder, data, sized ? &local : nullptr) : "";
			if (ok && !blankSave(data, saveFolder(rel)) && !why.empty()) { hold(rel, key, L, h, why, dl, false, errors); return false; }
			std::string md5 = hashStr(data, "md5");
			// (a stop waits for the save to be placed and recorded, with DraStic's .dsv in step: from here a game may start,
			// and one that finds them out of step takes the wrong save)
			KillHold placing;
			OpenOp op;
			Install in = !ok ? (err = "can't read the download", Install::Failed)
				: (!kept.empty() || md5 != h.hash) && !writeAtomic(dl, data) ? (err = "can't write the download", Install::Failed)
				: installSave(rel, dl, std::to_string(h.id), err, &L, md5 != h.hash ? h.hash : "", &op.id);
			op.md5 = md5;
			if (in != Install::Done) unlink(dl.c_str());
			if (in == Install::Blank) { say("  blank on the server, not taken: %s", baseOf(p).c_str()); return false; }
			if (in == Install::Failed) { errors.push_back(baseOf(p) + ": " + err); return false; }
			// (the newer save is in place: the menu's mark of one not fetched goes now, a stop that lands while this is
			// still being recorded leaving it otherwise, and the game would be asked about as if it played the older)
			if (beforeGame) unlink((RUN + "/launch.older").c_str());
			// (DraStic's .dsv brought into step with the DS save just installed, before either emulator starts: nds-saves
			// sees the .sav changed from what the two last held, and gives DraStic the new one. The file keeps the time
			// it was written at: a server's time on it would date it ahead of a card clock behind the server's, and
			// anything deciding by the files' times would then take it for the newer whatever was played since. The two
			// not in step after all: not agreed, so the next sync looks again, and holds the game while they differ)
			if (saveFolder(rel) == "nds" && ndsSaves(romPath(g.romRel)) != 0) { errors.push_back(baseOf(p) + ": DraStic's save isn't in step with it, decided next time"); return false; }
			// (a game started while the save was placed: it may have read the save before, and go on writing that one.
			// Not agreed, so the next sync decides again from what it then finds: the same bytes agree, others are a
			// conflict, held. Agreed, the game's would go up over the version just taken, as the newer)
			if (gameRunning()) { errors.push_back(baseOf(p) + ": a game started while it was placed, decided next time"); return false; }
			agree(key, md5, rel, h.id, md5 != h.hash ? h.hash : "");
			say("  down %s", p.c_str());
			saveConf(c);
			op.close();
			placing.end();
			if (!c.device.empty()) confirm(h);
			saveConf(c);
			return true;
		}
		// each negotiation's session closed, whatever came of the saves
		void complete(int done, int failed)
		{
			for (size_t i = 0; i < sessions.size(); i++)
			{
				std::string b = "{\"operations_completed\": " + std::to_string(i ? 0 : done) + ", \"operations_failed\": " + std::to_string(i ? 0 : failed) + "}";
				call("POST", "/api/sync/sessions/" + std::to_string(sessions[i]) + "/complete", &b);
			}
			sessions.clear();
		}
		bool sync(const std::string& kind, const std::map<std::string, std::string>& localAll, std::map<std::string, SaveSt>& state,
			int& up, int& down, int& conflicts, std::vector<std::string>& errors, std::string& note)
		{
			bool forSaves = kind == "save";
			std::string api = forSaves ? "/api/saves" : "/api/states", field = forSaves ? "saveFile" : "stateFile";
			std::string dev = c.device.empty() ? "" : "device_id=" + urlEsc(c.device);
			// the server's, per platform
			struct Srv { long long id, rom; std::string name, hash, updated; long long size; };
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
					// (a game not on the card: its saves not pulled, as Get games brings only what was asked for; one whose
					// saves stay on this handheld: neither way)
					std::string here = onCard(ri->second);
					if (here.empty() || keptHere(ri->second, here)) continue;
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
				Srv v{ (long long)s->num("id"), rom.id, name, lower(s->str("content_hash")), s->str("updated_at"), (long long)s->num("file_size_bytes") };
				auto e = srv.find(rel);
				if (e == srv.end() || e->second.updated < v.updated) srv[rel] = v;
			}
			std::set<std::string> paths;
			for (auto& kv : localAll)
				if (kv.second == kind && kv.first.compare(0, forSaves ? 6 : 7, forSaves ? "saves/" : "states/") == 0 && (!forSaves || canonical.count(kv.first)))
					paths.insert(kv.first);
			for (auto& kv : srv) paths.insert(kv.first);
			for (auto& kv : state) if (!forSaves || canonical.count(kv.first)) paths.insert(kv.first);
			int n = 0, total = (int)paths.size(), keptStates = 0;
			for (auto& rel : paths)
			{
				if (stopForGame(errors)) break;
				n++;
				status("running " + std::to_string(n) + " " + std::to_string(total) + (forSaves ? " Saves|" : " States|") + baseOf(rel));
				// the server's version taken: a save put in place; a state never, only kept in the vault for its owner, since
				// loading one puts the game's memory back over a newer battery save, and a state only loads in the core and
				// version that wrote it. RomM gives a state no checksum, so its size is what's checked
				auto take = [&](SaveSt ns, const auto& s)
				{
					if (forSaves) { if (download(api, s, rel, ns, dev, errors)) { state[rel] = ns; down++; } return; }
					if (dryRun) { say("  keep the server's state %s", rel.c_str()); return; }
					std::string kept, err, dl = RUN + "/sync-dl." + std::to_string(getpid());
					bool added = false;
					if (!fetchTo(c.server + api + "/" + std::to_string(s.id) + "/content", auth(), dl, s.size > 0 ? s.size : -1, "md5", "", err)) { dropDownload(dl); errors.push_back(rel + ": " + err); return; }
					bool ok = vaultKeep(rel, dl, kept, err, &added);
					unlink(dl.c_str());
					if (!ok) { errors.push_back(rel + ": the server's copy can't be kept in the vault: " + err); return; }
					ns.version = s.id; ns.server = s.updated;
					state[rel] = ns;
					if (added) keptStates++;
					say("  the server's state kept in the vault, not placed: %s", rel.c_str());
					saveConf(c);
				};
				std::string p = ROOT + "/" + rel;
				bool here = localAll.count(rel) > 0;
				auto si = srv.find(rel);
				auto st = state.find(rel);
				std::string md5 = here ? hashFile(p, "md5") : "";
				// (what this path holds back, under the slot's key for a save, the state's name for a state: worked out
				// again each time the save is looked at, so one settled by hand, or gone, is held no longer)
				std::string hk = si == srv.end() ? "" : std::to_string(si->second.rom) + (forSaves ? "/" + SLOT : "/state:" + baseOf(rel));
				if (!forSaves && !settleKey.empty() && hk != settleKey) continue;	// (one state's choice: no other state's sync)
				SlotNote heldBefore;
				if (!hk.empty() && c.held.count(hk)) { heldBefore = c.held[hk]; c.held.erase(hk); }
				if (here && !md5.empty() && blankSave(readFile(p), saveFolder(rel)) && (st == state.end() || st->second.hash != md5))
				{
					if (st != state.end())
					{
						if (forSaves && !hk.empty()) c.held[hk] = { rel, NOTE_BLANK, md5, si->second.hash, "", si->second.updated, "", si->second.id, romOf(rel) };	// (hk: the server has it)
						say("  %s, left as it is: %s", NOTE_BLANK, rel.c_str());
						continue;
					}
					if (si == srv.end() || si->second.hash == md5) continue;
					SaveSt ns; ns.hash = md5;	// (what the download checks it is replacing)
					take(ns, si->second);
					continue;
				}
				if (!here)
				{
					// (a state the card doesn't hold: its record stays while the server has it, with no hash for the card,
					// so a version already in the vault isn't fetched at every sync, and a new one is)
					if (!forSaves && st != state.end() && si != srv.end() && st->second.version == si->second.id && st->second.server == si->second.updated)
					{
						st->second.hash.clear();
						continue;
					}
					// (a save gone from the card since it synced: left as it is, said, never put back by itself; the record
					// stays, so the next run doesn't take the server's over nothing. The last good copy is in the vault)
					if (forSaves && st != state.end())
					{
						if (!hk.empty()) c.held[hk] = { rel, NOTE_MISSING, "", si->second.hash, "", si->second.updated, "", si->second.id, romOf(rel) };	// (hk: the server has it)
						say("  %s, left as it is: %s", NOTE_MISSING, rel.c_str());
						continue;
					}
					if (si == srv.end()) { if (st != state.end()) state.erase(st); continue; }
					if (exists(p)) continue;	// (a file the rules leave out)
					take(SaveSt(), si->second);
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
					if (upload(api, field, rom.id, p, rel, md5, state, dev, errors)) up++;
					continue;
				}
				const Srv& s = si->second;
				bool known = st != state.end();
				bool localChanged = !known || st->second.hash != md5;
				bool serverChanged = !known || st->second.version != s.id || (st->second.server != s.updated && (s.hash.empty() || s.hash != st->second.hash));
				if (!s.hash.empty() && s.hash == md5)
				{
					// (agreed: its last good copy in the vault, as a save sent or taken has, when the record is new)
					if (forSaves && (!known || st->second.hash != md5)) vaultGood(rel, readFile(p));
					state[rel] = { (double)s.id, md5, s.updated };
					continue;
				}
				if (!localChanged && !serverChanged) continue;
				if (localChanged && !serverChanged) { if (upload(api, field, s.rom, p, rel, md5, state, dev, errors)) up++; continue; }
				if (!localChanged && serverChanged) { take(st->second, s); continue; }
				// both changed, or a card that never synced holding another version: held, both kept (the server's copy in
				// the vault, fetched once while its version is the same), neither sent nor taken until its owner chooses.
				// Never the newer by the files' times: the card's clock may be years out, and the version it would write
				// over on the server, or here, is the other's progress
				// (its owner chose the card's over the version they saw. A state goes up by its name, over the server's, a
				// copy of which is in the vault; a RomM before 5.4 takes no save from here, so the card's stays unsent and
				// that server version counts as seen. A version moved since is held again, what it now is)
				if (hk == settleKey && heldBefore.saveId == s.id && heldBefore.updated == s.updated && !dryRun)
				{
					if (!sendOk) { say("  this handheld's kept, not sent: RomM %s takes nothing from here: %s", c.serverVersion.c_str(), rel.c_str()); state[rel] = { (double)s.id, md5, s.updated }; saveConf(c); continue; }
					// (that upload replaces the server's where it is, so the copy in the vault must really be there)
					if (heldBefore.vault.empty() || hashFile(heldBefore.vault, "md5") != heldBefore.serverMd5) { errors.push_back(rel + ": the server's copy isn't in the vault, not sent"); c.held[hk] = heldBefore; continue; }
					// (sent over it, the copy in the vault is the only one of that version anywhere; nothing removes it)
					if (upload(api, field, s.rom, p, rel, md5, state, dev, errors)) { up++; saveConf(c); }
					else c.held[hk] = heldBefore;
					continue;
				}
				conflicts++;
				if (dryRun) { say("  conflict %s", rel.c_str()); continue; }
				SlotNote nt{ rel, forSaves ? "changed here and on the server" : "a state changed here and on the server", md5, "", "", s.updated, "", s.id };
				nt.serverMd5 = serverHash(api, s);
				nt.rom = romOf(rel);
				if (!forSaves) { auto ri = romById.find(s.rom); if (ri != romById.end()) nt.rom = onCard(ri->second); }
				// (a failure below keeps what was held: the copy kept, and the note that says so)
				auto keepNote = [&] { if (!heldBefore.rel.empty()) c.held[hk] = heldBefore; };
				if (nt.serverMd5.empty() && forSaves) { errors.push_back(rel + ": the server gives no checksum for its copy"); keepNote(); continue; }
				// (the copy held already, while the server's is that version. A state has no checksum, and an upload of the
				// same name keeps its id, so its time says so)
				if (heldBefore.saveId == s.id && !heldBefore.vault.empty() && exists(heldBefore.vault) &&
					(forSaves ? hashFile(heldBefore.vault, "md5") == nt.serverMd5 : heldBefore.updated == s.updated))
				{
					nt.vault = heldBefore.vault;
					nt.serverMd5 = heldBefore.serverMd5;
				}
				if (nt.vault.empty())
				{
					std::string e2, dl = RUN + "/sync-dl." + std::to_string(getpid());
					if (!fetchTo(c.server + api + "/" + std::to_string(s.id) + "/content", auth(), dl, !forSaves && s.size > 0 ? s.size : -1, "md5", nt.serverMd5, e2)) { dropDownload(dl); errors.push_back(rel + ": " + e2); keepNote(); continue; }
					if (!forSaves) nt.serverMd5 = hashFile(dl, "md5");
					// (the same state on both sides, which only its bytes could say: nothing to hold)
					if (nt.serverMd5 == md5) { unlink(dl.c_str()); conflicts--; state[rel] = { (double)s.id, md5, s.updated }; saveConf(c); continue; }
					bool kept = vaultKeep(rel, dl, nt.vault, e2);
					unlink(dl.c_str());
					if (!kept) { errors.push_back(rel + ": the server's copy can't be kept in the vault: " + e2); keepNote(); continue; }
				}
				else if (nt.serverMd5 == md5) { conflicts--; state[rel] = { (double)s.id, md5, s.updated }; saveConf(c); continue; }	// (settled by hand)
				c.held[hk] = nt;
				say("  conflict, both kept: %s (the server's in the vault)", rel.c_str());
				saveConf(c);
			}
			if (keptStates) addNote(note, std::to_string(keptStates) + (keptStates == 1 ? " state from the server kept in the vault" : " states from the server kept in the vault"));
			saveConf(c);
			return true;
		}
		bool upload(const std::string& api, const std::string& field, long long romId, const std::string& p, const std::string& rel,
			const std::string& md5, std::map<std::string, SaveSt>& state, const std::string& dev, std::vector<std::string>& errors)
		{
			if (!sendOk) return false;	// (an older RomM: kept here, said once in the note)
			if (dryRun) { say("  upload %s", rel.c_str()); return true; }
			// a copy of the file as it is now: the emulator can't change what is sent halfway
			std::string snap = RUN + "/sync-upload." + std::to_string(getpid());
			std::string body = readFile(p);
			if (blankSave(body, saveFolder(rel))) { say("  blank, not sent: %s", rel.c_str()); return false; }
			if (hashStr(body, "md5") != md5 || !writeAtomic(snap, body)) { errors.push_back(rel + ": changed while syncing, next time"); unlink(snap.c_str()); return false; }
			Req q; q.method = "POST"; q.headers = auth(); q.timeout = 0;
			q.url = c.server + api + "?rom_id=" + std::to_string(romId) + (api == "/api/saves" && !dev.empty() ? "&" + dev : "") + "&emulator=" + urlEsc(baseOf(dirOf(rel)) == "nds" ? "bananaos-nds" : "retroarch");
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
		template <class S> bool download(const std::string& api, const S& s, const std::string& rel, SaveSt& st, const std::string& dev, std::vector<std::string>& errors)
		{
			std::string p = ROOT + "/" + rel;
			if (dryRun) { say("  download %s", p.c_str()); return false; }
			std::string before = exists(p) ? st.hash : "";
			if (exists(p) && hashFile(p, "md5") != st.hash) { errors.push_back(baseOf(p) + ": changed while syncing, next time"); return false; }
			std::string want = serverHash(api, s), err;
			if (want.empty()) { errors.push_back(baseOf(p) + ": the server gives no checksum for it, not taken"); return false; }
			// (fetched to /run first: nothing on the card changes until it is all here and checked)
			std::string url = c.server + api + "/" + std::to_string(s.id) + "/content" + (api == "/api/saves" && !dev.empty() ? "?" + dev : "");
			std::string dl = RUN + "/sync-dl." + std::to_string(getpid());
			OpenOp op{ "", want };
			Install in = fetchTo(url, auth(), dl, -1, "md5", want, err) ? installSave(rel, dl, std::to_string(s.id), err, &before, "", &op.id) : Install::Failed;
			if (in == Install::Blank) { say("  blank on the server, not taken: %s", baseOf(p).c_str()); return false; }
			if (in == Install::Failed)
			{
				dropDownload(dl);
				errors.push_back(baseOf(p) + ": " + err);
				return false;
			}
			// (DraStic's .dsv brought into step with the DS save just installed, before either emulator starts)
			auto ci = canonical.find(rel);
			if (ci != canonical.end() && saveFolder(rel) == "nds" && ndsSaves(romPath(ci->second.romRel)) != 0) { errors.push_back(baseOf(p) + ": DraStic's save isn't in step with it, decided next time"); return false; }
			// (a game started while it was placed: not recorded, so the next sync decides again from what it finds)
			if (gameRunning()) { errors.push_back(baseOf(p) + ": a game started while it was placed, decided next time"); return false; }
			st = { (double)s.id, want, s.updated };
			say("  down %s", p.c_str());
			saveConf(c);
			op.close();
			return true;
		}
	};
	}

	std::unique_ptr<Backend> newRomm(Conf& c) { return std::unique_ptr<Backend>(new Romm(c)); }
}
