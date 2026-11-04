// the save vault and the journal (save_vault.h)
#include "save_vault.h"
#include "save_adapt.h"
#include "sync_core.h"
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <unistd.h>
#include <vector>

namespace bsync
{
	std::string vaultDir() { return ROOT + "/banana/save-vault"; }
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

	Install installSave(const std::string& rel, const std::string& dl, const std::string& serverId, std::string& err)
	{
		bool ok;
		std::string data = readFile(dl, &ok), p = ROOT + "/" + rel;
		if (!ok) { err = "can't read the download"; return Install::Failed; }
		if (blankSave(data, saveFolder(rel))) { unlink(dl.c_str()); return Install::Blank; }	// (empty included)
		std::string want = hashStr(data, "md5"), before = exists(p) ? hashFile(p, "md5") : "", kept;
		if (before == want) { vaultGood(rel, data); unlink(dl.c_str()); return Install::Done; }
		if (!before.empty() && !vaultKeep(rel, p, kept, err)) return Install::Failed;
		std::string id = journalBegin({ "install", rel, before, serverId, want, kept, want });
		if (id.empty()) { err = "the sync's journal can't be written (card full?)"; return Install::Failed; }
		if (!writeAtomic(p, data) || hashFile(p, "md5") != want)
		{
			journalEnd(id, "failed", exists(p) ? hashFile(p, "md5") : "");
			err = "can't write " + rel + " (card full?)";
			return Install::Failed;
		}
		vaultGood(rel, data);
		journalEnd(id, "ok", want);
		unlink(dl.c_str());
		return Install::Done;
	}

	std::string journalRecover()
	{
		std::ifstream f(journalPath());
		if (!f) return "";
		std::map<std::string, std::vector<std::string>> open;
		for (std::string l; std::getline(f, l);)
		{
			auto v = fields(l);
			if (v[0] == "begin" && v.size() >= 10) open[v[1]] = v;
			else if (v[0] == "end" && v.size() >= 2) open.erase(v[1]);
		}
		f.close();
		std::string out;
		for (auto& kv : open)
		{
			const auto& b = kv.second;
			std::string rel = b[3], p = ROOT + "/" + rel, h = exists(p) ? hashFile(p, "md5") : "";
			std::string res = h == b[4] ? "unchanged" : h == b[8] ? "installed" : "changed-since";
			if (safeRel(rel)) unlink((p + ".new").c_str());
			if (!journalEnd(kv.first, "recovered " + res, h)) return out;
			out += res + " " + rel + "\n";
		}
		// (rotated only once nothing is left open: the begin lines above are what the next start reads)
		if (fileSize(journalPath()) > (1 << 20)) rename(journalPath().c_str(), (journalPath() + ".1").c_str());
		return out;
	}
}
