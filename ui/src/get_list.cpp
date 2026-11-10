#include "get_list.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <sys/file.h>
#include <unistd.h>

namespace bsync
{
	std::vector<std::string> readListLines(const std::string& path)
	{
		std::vector<std::string> out;
		std::ifstream f(path);
		for (std::string l; std::getline(f, l);)
		{
			while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
			if (!l.empty() && l[0] != '#') out.push_back(l);
		}
		return out;
	}

	// written whole beside it, flushed to the card and renamed over it, the folder then flushed: a power cut leaves the
	// old list or the new one, and a write that fails (a full card) leaves the old one. Only this file and its folder
	// are flushed, not every filesystem: a download writing to the same card doesn't hold up a press on the page
	bool writeListLines(const std::string& path, const std::string& header, const std::vector<std::string>& lines)
	{
		std::string text = "# " + header + "\n";
		for (auto& l : lines) text += l + "\n";
		const std::string tmp = path + ".new";
		FILE* w = fopen(tmp.c_str(), "w");
		if (!w) return false;
		bool ok = fwrite(text.data(), 1, text.size(), w) == text.size();
		ok &= fflush(w) == 0 && fsync(fileno(w)) == 0;
		ok &= fclose(w) == 0;
		if (!ok || rename(tmp.c_str(), path.c_str()) != 0) { unlink(tmp.c_str()); return false; }
		const size_t slash = path.rfind('/');
		int d = open(slash == std::string::npos ? "." : path.substr(0, slash).c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (d >= 0) { fsync(d); close(d); }
		return true;
	}

	namespace
	{
		int lockGet(const std::string& runDir)
		{
			int lk = open((runDir + "/get.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
			if (lk >= 0) while (flock(lk, LOCK_EX) != 0 && errno == EINTR) {}
			return lk;
		}
		std::map<std::string, std::string> readFailed(const std::string& fp)
		{
			std::map<std::string, std::string> why;
			for (auto& l : readListLines(fp)) { size_t t = l.find('\t'); if (t != std::string::npos) why[l.substr(0, t)] = l.substr(t + 1); }
			return why;
		}
		// the failed file as why has it, for the games listed: unchanged left alone, none no file (the page takes none
		// as none)
		bool writeFailed(const std::string& fp, const std::set<std::string>& listed, const std::map<std::string, std::string>& why,
			const std::map<std::string, std::string>& was)
		{
			std::vector<std::string> lines;
			std::map<std::string, std::string> kept;
			for (auto& kv : why)
			{
				if (!listed.count(kv.first)) continue;
				std::string w = kv.second;
				for (char& ch : w) if (ch == '\t' || ch == '\n') ch = ' ';
				kept[kv.first] = w;
				lines.push_back(kv.first + "\t" + w);
			}
			if (kept == was) return true;
			if (lines.empty()) { unlink(fp.c_str()); return true; }
			return writeListLines(fp, "Home server > Get games: why a game still on the list didn't come last time", lines);
		}
	}

	bool editGetList(const std::string& bananaDir, const std::string& runDir, const std::vector<std::string>& add,
		const std::set<std::string>& drop, int* waiting)
	{
		int lk = lockGet(runDir);
		const std::string path = bananaDir + "/homesync-get.txt", fp = bananaDir + "/homesync-get-failed.txt";
		std::vector<std::string> now = readListLines(path), out;
		std::set<std::string> seen;
		for (auto& k : now) if (!drop.count(k) && seen.insert(k).second) out.push_back(k);
		for (auto& k : add) if (!drop.count(k) && seen.insert(k).second) out.push_back(k);
		bool ok = out == now || writeListLines(path, "Home server > Get games: the games to download, taken off as each comes", out);
		const auto was = readFailed(fp);
		auto why = was;
		for (auto& k : drop) why.erase(k);
		for (auto& k : add) why.erase(k);
		if (why != was) ok &= writeFailed(fp, std::set<std::string>(seen.begin(), seen.end()), why, was);
		if (lk >= 0) close(lk);
		if (waiting) *waiting = (int)(ok ? out : now).size();
		return ok;
	}

	bool editGetFailed(const std::string& bananaDir, const std::string& runDir,
		const std::function<void(const std::set<std::string>& listed, std::map<std::string, std::string>& why)>& fn)
	{
		int lk = lockGet(runDir);
		const std::string fp = bananaDir + "/homesync-get-failed.txt";
		auto q = readListLines(bananaDir + "/homesync-get.txt");
		const std::set<std::string> listed(q.begin(), q.end());
		const auto was = readFailed(fp);
		auto why = was;
		fn(listed, why);
		bool ok = writeFailed(fp, listed, why, was);
		if (lk >= 0) close(lk);
		return ok;
	}
}
