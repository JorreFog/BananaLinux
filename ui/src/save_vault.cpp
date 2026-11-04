// the save vault and the journal (save_vault.h)
#include "save_vault.h"
#include "save_adapt.h"
#include "sync_core.h"
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace bsync
{
	std::string vaultDir() { return ROOT + "/banana/save-vault"; }
	static void listKept(const std::string& dir, std::vector<Kept>& out)
	{
		DIR* d = opendir(dir.c_str());
		if (!d) return;
		std::vector<std::string> names;
		while (dirent* e = readdir(d)) if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) names.push_back(e->d_name);
		closedir(d);
		std::sort(names.begin(), names.end());
		std::string top = vaultDir() + "/";
		for (auto& n : names)
		{
			std::string p = dir + "/" + n;
			struct stat st;
			if (lstat(p.c_str(), &st) != 0) continue;
			if (S_ISDIR(st.st_mode)) { listKept(p, out); continue; }
			size_t dot = n.rfind('.');
			if (!S_ISREG(st.st_mode) || dot == std::string::npos || workFile(n) || dir.size() <= top.size()) continue;
			out.push_back({ dirOf(dir).substr(std::min(top.size(), dirOf(dir).size())) + "/" + baseOf(dir) + n.substr(dot), p, (long long)st.st_size });
		}
	}
	std::vector<Kept> vaultList()
	{
		std::vector<Kept> out;
		listKept(vaultDir(), out);
		return out;
	}
	std::string vaultFile(const std::string& path)
	{
		char a[PATH_MAX], b[PATH_MAX];
		if (!realpath(vaultDir().c_str(), a) || !realpath(path.c_str(), b)) return "";
		std::string top = std::string(a) + "/", real = b;
		struct stat st;
		return real.compare(0, top.size(), top) == 0 && stat(b, &st) == 0 && S_ISREG(st.st_mode) ? real : "";
	}
	std::string journalPath() { return ROOT + "/banana/logs/sync/journal.log"; }

	namespace
	{
	std::string utcStamp()
	{
		char b[32]; time_t t = time(nullptr); struct tm tm; gmtime_r(&t, &tm);
		strftime(b, sizeof b, "%Y%m%dT%H%M%SZ", &tm);
		return b;
	}
	std::string extPart(const std::string& rel) { std::string e = extOf(baseOf(rel)); return e.empty() ? "" : "." + e; }
	std::string gameDir(const std::string& rel) { return vaultDir() + "/" + dirOf(rel) + "/" + stemOf(baseOf(rel)); }
	}

	bool vaultKeep(const std::string& rel, const std::string& src, std::string& kept, std::string& err, bool* added)
	{
		if (added) *added = false;
		if (!safeRel(rel) || rel.find('/') == std::string::npos) { err = "not a save path: " + rel; return false; }
		bool ok;
		std::string data = readFile(src, &ok);
		if (!ok) { err = "can't read " + src; return false; }
		std::string md5 = hashStr(data, "md5"), dir = gameDir(rel), tail = "-" + md5.substr(0, 8) + extPart(rel);
		if (DIR* d = opendir(dir.c_str()))
		{
			std::string found;
			while (dirent* e = readdir(d))
			{
				std::string n = e->d_name;
				if (n.size() > tail.size() && n.compare(n.size() - tail.size(), tail.size(), tail) == 0 && hashFile(dir + "/" + n, "md5") == md5)
					found = dir + "/" + n;
			}
			closedir(d);
			if (!found.empty()) { kept = found; return true; }
		}
		kept = dir + "/" + utcStamp() + tail;
		for (int i = 2; exists(kept); i++) kept = dir + "/" + utcStamp() + "-" + std::to_string(i) + tail;
		if (!writeAtomic(kept, data)) { err = "can't keep a copy in the vault (card full?)"; unlink(kept.c_str()); return false; }
		if (hashFile(kept, "md5") != md5) { err = "the vault's copy reads back wrong"; unlink(kept.c_str()); return false; }
		if (added) *added = true;
		return true;
	}

	bool vaultGood(const std::string& rel, const std::string& data)
	{
		if (blankSave(data, saveFolder(rel)) || !safeRel(rel)) return false;
		std::string p = gameDir(rel) + "/last-good" + extPart(rel);
		return writeAtomic(p, data) && hashFile(p, "md5") == hashStr(data, "md5");
	}

	// ---- the journal: tab-separated lines, "begin id op rel before server-id server-hash vault want time" and
	// "end id result after time", each appended and synced before the step it describes
	namespace
	{
	bool journalLine(const std::string& line)
	{
		std::string p = journalPath();
		mkdirs(dirOf(p));
		bool fresh = !exists(p);
		int fd = open(p.c_str(), O_RDWR | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
		if (fd < 0) return false;
		// (a line torn by a power cut has no newline: this one starts on its own, the torn one under 10 fields, ignored)
		char last = '\n';
		off_t end = lseek(fd, 0, SEEK_END);
		if (end > 0 && pread(fd, &last, 1, end - 1) != 1) last = '\n';
		std::string l = std::string(last == '\n' ? "" : "\n") + line + "\t" + utcStamp() + "\n";
		bool ok = write(fd, l.data(), l.size()) == (ssize_t)l.size() && fsync(fd) == 0;
		ok &= close(fd) == 0;
		return ok && (!fresh || syncDir(dirOf(p)));
	}
	std::vector<std::string> fields(const std::string& l)
	{
		std::vector<std::string> f;
		size_t a = 0;
		while (true) { size_t b = l.find('\t', a); f.push_back(l.substr(a, b - a)); if (b == std::string::npos) break; a = b + 1; }
		return f;
	}
	}

	std::string journalBegin(const JOp& o)
	{
		static int n = 0;
		for (auto* s : { &o.op, &o.rel, &o.before, &o.serverId, &o.serverHash, &o.vault, &o.want })
			if (s->find_first_of("\t\n") != std::string::npos) return "";
		std::string id = std::to_string(getpid()) + "." + std::to_string(time(nullptr)) + "." + std::to_string(++n);
		return journalLine("begin\t" + id + "\t" + o.op + "\t" + o.rel + "\t" + o.before + "\t" + o.serverId + "\t" + o.serverHash + "\t" + o.vault + "\t" + o.want) ? id : "";
	}
	bool journalEnd(const std::string& id, const std::string& result, const std::string& after)
	{
		return journalLine("end\t" + id + "\t" + result + "\t" + after);
	}

	// the quick-resume states that belong with the save being replaced, removed before it is: loaded over the new save,
	// one puts the old save's memory back, and the emulator then writes that over the new save at its next save, which
	// the sync would send as the newer. BananaDS's resume state (DraStic's goes when nds-saves gives it the new save),
	// and RetroArch's automatic slot (savestate_auto_save, Quick resume), in the core's folder under states/: every GBA
	// core reads saves/gba, each with a states folder of its own (the cores run-retroarch knows, and the GBA row of the
	// cores table in settings.cpp lists: a shell script and the menu's own table, neither linked into this tool). A save
	// in the flat saves/ doesn't say its core, whose states are still sorted into a folder of its own name: every
	// folder under states/ but the DS's is looked in. With sorted states off in RetroArch's menu, every core's are in
	// states/ itself. Either way one of another system's game of the same name goes too, a resume state at most,
	// never a save
	static void dropResumeStates(const std::string& rel)
	{
		if (rel.compare(0, 6, "saves/") != 0) return;
		std::string folder = saveFolder(rel), stem = stemOf(baseOf(rel));
		if (folder == "nds") { unlink((ROOT + "/states/nds/" + stem + ".resume.mst").c_str()); return; }
		std::vector<std::string> cores;
		if (folder == "gba") cores = { "gpSP", "mGBA", "VBA Next", "VBA-M", "Meteor" };
		else if (!folder.empty()) cores = { folder };
		else if (DIR* d = opendir((ROOT + "/states").c_str()))
		{
			while (dirent* e = readdir(d)) { std::string n = e->d_name; if (n[0] != '.' && n != "nds") cores.push_back(n); }
			closedir(d);
		}
		for (auto& core : cores) unlink((ROOT + "/states/" + core + "/" + stem + ".state.auto").c_str());
		unlink((ROOT + "/states/" + stem + ".state.auto").c_str());	// (with sorted states off in RetroArch's menu)
	}

	Install installSave(const std::string& rel, const std::string& dl, const std::string& serverId, std::string& err, const std::string* expect, const std::string& serverHash, std::string* openId)
	{
		if (openId) openId->clear();
		bool ok;
		std::string data = readFile(dl, &ok), p = ROOT + "/" + rel;
		if (!ok) { err = "can't read the download"; return Install::Failed; }
		if (blankSave(data, saveFolder(rel))) { unlink(dl.c_str()); return Install::Blank; }	// (empty included)
		std::string want = hashStr(data, "md5"), before = exists(p) ? hashFile(p, "md5") : "", kept;
		// (a save there that can't be read isn't one that isn't there: nothing could be kept of it)
		if (exists(p) && before.empty()) { err = "can't read " + rel; return Install::Failed; }
		if (before == want) { vaultGood(rel, data); unlink(dl.c_str()); return Install::Done; }
		// (the save is no longer the one the decision was made on: an emulator wrote it meanwhile)
		if (expect && before != *expect) { err = "changed while syncing, next time"; return Install::Failed; }
		// (a run of one game's goes on when a game starts, and that may be this game, its save already read: what goes
		// under it now would be written over by the game, and then sent as the newer. The next sync decides it again)
		if (gameRunning()) { err = "a game started, next time"; return Install::Failed; }
		if (!before.empty() && !vaultKeep(rel, p, kept, err)) return Install::Failed;
		std::string id = journalBegin({ "install", rel, before, serverId, serverHash.empty() ? want : serverHash, kept, want });
		if (id.empty()) { err = "the sync's journal can't be written (card full?)"; return Install::Failed; }
		// (again, the vault's copy and the journal's syncs behind it: on a busy card they take longer than a game takes to
		// start, the menu having waited out save.lock for a moment only)
		if (gameRunning()) { journalEnd(id, "failed", before); err = "a game started, next time"; return Install::Failed; }
		// (before the save changes: a state left beside the new save would be loaded over it. A write that then fails
		// costs a resume state, not a save)
		dropResumeStates(rel);
		if (!writeAtomic(p, data) || hashFile(p, "md5") != want)
		{
			journalEnd(id, "failed", exists(p) ? hashFile(p, "md5") : "");
			err = "can't write " + rel + " (card full?)";
			return Install::Failed;
		}
		vaultGood(rel, data);
		// (left open for a caller that records the agreement: a run killed before that is agreed by the next start's
		// recovery, as one killed mid-write is. Said to be in place first, so the agreement holds even when the game was
		// played over it before that start: what it played on is this version)
		if (openId) { journalLine("written\t" + id); *openId = id; }
		else journalEnd(id, "ok", want);
		unlink(dl.c_str());
		return Install::Done;
	}

	std::string journalRecover(Conf* c, const std::function<bool()>& persist)
	{
		std::ifstream f(journalPath());
		if (!f) return "";
		std::map<std::string, std::vector<std::string>> open;
		std::set<std::string> written;	// (put in place, the run killed before it recorded the agreement)
		for (std::string l; std::getline(f, l);)
		{
			auto v = fields(l);
			if (v[0] == "begin" && v.size() >= 10) open[v[1]] = v;
			else if (v[0] == "written" && v.size() >= 2) written.insert(v[1]);
			else if (v[0] == "end" && v.size() >= 2) { open.erase(v[1]); written.erase(v[1]); }
		}
		f.close();
		std::string out;
		struct End { std::string id, result, after; };	// (after: the save's MD5 now)
		std::vector<End> ends;
		bool agreed = false;
		for (auto& kv : open)
		{
			const auto& b = kv.second;
			std::string rel = b[3], p = ROOT + "/" + rel, h = exists(p) ? hashFile(p, "md5") : "";
			// (the new file whole beside the name, synced before the rename, and nothing under the name: the power went
			// in the rename itself, which this filesystem doesn't make atomic. Put in place, as it was about to be)
			if (!exists(p) && safeRel(rel) && exists(p + ".new") && hashFile(p + ".new", "md5") == b[8] && rename((p + ".new").c_str(), p.c_str()) == 0) { syncDir(dirOf(p)); h = b[8]; }
			std::string res = h == b[4] ? "unchanged" : h == b[8] ? "installed" : "changed-since";
			if (safeRel(rel)) unlink((p + ".new").c_str());
			// (in place, and the run killed before it could say so: agreed now, as it would have been. The record of a
			// slot that held the save it replaced, else none: a save changed since is left for the sync to decide. A kept
			// copy put back has no server version: not agreed, so the next sync sends it as the change it is. The server's
			// own MD5 for the version (a .dsv's, with its footer) kept beside, as the slot's wire)
			// (in place and played on since, a game started before this start: agreed on the version put in place, so the
			// game's save is a change from it, sent as one, not a conflict with it)
			long long id = atoll(b[5].c_str());
			bool placed = res == "installed" || (res == "changed-since" && written.count(kv.first));
			if (placed && c && id > 0)
			{
				for (auto& s : c->slots)
					if (s.second.rel == rel && s.second.md5 == b[4]) { s.second.md5 = b[8]; s.second.saveId = id; s.second.wire = b[6] != b[8] ? b[6] : ""; s.second.lost = false; agreed = true; }
				// (Romhost's and an older RomM's records, kept by the file: the version in place is the server's. Romhost's
				// by its SHA-1, read off the save, so only while the save is that version still)
				auto sv = c->saves.find(rel);
				if (sv != c->saves.end() && (res == "installed" || c->backend != "romhost")) { sv->second = { (double)id, c->backend == "romhost" ? hashFile(p, "sha1") : b[8], "" }; agreed = true; }
			}
			ends.push_back({ kv.first, res, h });
			out += res + " " + rel + "\n";
		}
		if (agreed && persist && !persist()) return out;
		for (auto& e : ends)
			if (!journalEnd(e.id, "recovered " + e.result, e.after)) return out;
		// (rotated only once nothing is left open: the begin lines above are what the next start reads)
		if (fileSize(journalPath()) > (1 << 20)) rename(journalPath().c_str(), (journalPath() + ".1").c_str());
		return out;
	}
}
