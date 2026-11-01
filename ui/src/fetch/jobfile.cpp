#include "jobfile.h"
#include "names.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace banana
{
	namespace fetch
	{
		std::string escapeField(const std::string& s)
		{
			std::string r;
			for (char c : s)
			{
				if (c == '\\') r += "\\\\";
				else if (c == '\t') r += "\\t";
				else if (c == '\n') r += "\\n";
				else if (c == '\r') r += "\\r";
				else r += c;
			}
			return r;
		}

		std::string unescapeField(const std::string& s)
		{
			std::string r;
			for (size_t i = 0; i < s.size(); i++)
			{
				if (s[i] != '\\' || i + 1 >= s.size()) { r += s[i]; continue; }
				char c = s[++i];
				r += c == 't' ? '\t' : c == 'n' ? '\n' : c == 'r' ? '\r' : c;
			}
			return r;
		}

		std::vector<std::string> splitFields(const std::string& line)
		{
			std::vector<std::string> out;
			size_t a = 0;
			for (;;)
			{
				size_t b = line.find('\t', a);
				out.push_back(unescapeField(line.substr(a, b == std::string::npos ? std::string::npos : b - a)));
				if (b == std::string::npos) break;
				a = b + 1;
			}
			return out;
		}

		std::string joinFields(const std::vector<std::string>& fields)
		{
			std::string r;
			for (size_t i = 0; i < fields.size(); i++) r += (i ? "\t" : "") + escapeField(fields[i]);
			return r;
		}

		static bool writeAtomic(const std::string& path, const std::string& text)
		{
			std::string tmp = path + ".new";
			FILE* f = fopen(tmp.c_str(), "w");
			if (!f) return false;
			bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
			ok &= fflush(f) == 0 && fsync(fileno(f)) == 0;
			ok &= fclose(f) == 0;
			if (!ok || rename(tmp.c_str(), path.c_str()) != 0) { unlink(tmp.c_str()); return false; }
			return true;
		}

		bool readJob(const std::string& path, Job& j)
		{
			std::ifstream f(path);
			if (!f) return false;
			j = Job();
			j.path = path;
			std::string ln;
			while (std::getline(f, ln))
			{
				if (!ln.empty() && ln.back() == '\r') ln.pop_back();
				if (ln.empty() || ln[0] == '#') continue;
				if (ln.compare(0, 4, "get\t") == 0 || ln.compare(0, 8, "extract\t") == 0)
				{
					std::vector<std::string> x = splitFields(ln);
					JobItem it;
					it.extract = x[0] == "extract";
					if (!it.extract && x.size() >= 4) { it.size = atoll(x[1].c_str()); it.remote = x[2]; it.local = x[3]; }
					else if (it.extract && x.size() >= 3) { it.remote = x[1]; it.local = x[2]; }
					else continue;
					j.items.push_back(it);
					continue;
				}
				size_t eq = ln.find('=');
				if (eq == std::string::npos) continue;
				std::string k = trim(ln.substr(0, eq)), v = trim(ln.substr(eq + 1));
				if (k == "title") j.title = v;
				else if (k == "server") j.server = v;
				else if (k == "unpack") j.unpack = v == "yes";
				else if (k == "keep_zips") j.keepZips = v == "yes";
				else if (k == "delete_archive") j.deleteArchive = v == "yes";
				else if (k == "unwrap") j.unwrap = v != "no";
				else if (k == "ignore_memory") j.ignoreMemory = v == "yes";
				else if (k == "temp_root") j.tempRoot = v;
				else if (k == "card_dev") j.cardDev = strtoull(v.c_str(), nullptr, 10);
				else if (k == "archive_password_file") j.archivePasswordFile = v;
			}
			return true;
		}

		bool writeJob(const std::string& path, const Job& j)
		{
			std::ostringstream o;
			auto line = [](std::string v) { for (auto& c : v) if (c == '\n' || c == '\r') c = ' '; return v; };
			o << "# A Files job: banana-ui wrote it, banana-fetch does it (what happened to each item: beside it, in .done)\n";
			o << "title = " << line(j.title) << "\n";
			if (!j.server.empty()) o << "server = " << line(j.server) << "\n";
			o << "unpack = " << (j.unpack ? "yes" : "no") << "\nkeep_zips = " << (j.keepZips ? "yes" : "no") << "\n";
			o << "delete_archive = " << (j.deleteArchive ? "yes" : "no") << "\n";
			o << "unwrap = " << (j.unwrap ? "yes" : "no") << "\nignore_memory = " << (j.ignoreMemory ? "yes" : "no") << "\n";
			o << "temp_root = " << line(j.tempRoot) << "\n";
			if (j.cardDev) o << "card_dev = " << j.cardDev << "\n";
			if (!j.archivePasswordFile.empty()) o << "archive_password_file = " << line(j.archivePasswordFile) << "\n";
			for (auto& it : j.items)
			{
				if (it.extract) o << joinFields({ "extract", it.remote, it.local }) << "\n";
				else o << joinFields({ "get", std::to_string(it.size), it.remote, it.local }) << "\n";
			}
			return writeAtomic(path, o.str());
		}

		std::vector<Outcome> readOutcomes(const std::string& path)
		{
			std::vector<Outcome> out;
			std::ifstream f(path);
			std::string ln;
			while (std::getline(f, ln))
			{
				std::vector<std::string> x = splitFields(ln);
				if (x.size() < 2 || x[0].empty()) continue;
				Outcome o;
				o.index = atoi(x[0].c_str());
				o.what = x[1];
				o.detail = x.size() > 2 ? x[2] : "";
				// (a later line for the same item counts: downloaded, then unpacked)
				bool found = false;
				for (auto& e : out) if (e.index == o.index) { e = o; found = true; }
				if (!found) out.push_back(o);
			}
			return out;
		}

		void addOutcome(const std::string& path, const Outcome& o)
		{
			std::string l = joinFields({ std::to_string(o.index), o.what, o.detail }) + "\n";
			int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
			if (fd < 0) return;
			if (write(fd, l.data(), l.size()) < 0) {}
			fsync(fd);
			close(fd);
		}

		static std::vector<std::string> named(const std::string& dir, const char* suffix)
		{
			std::vector<std::string> out;
			size_t n = strlen(suffix);
			if (DIR* d = opendir(dir.c_str()))
			{
				while (dirent* e = readdir(d))
				{
					std::string nm = e->d_name;
					if (nm.size() > n && nm[0] != '.' && nm.compare(nm.size() - n, n, suffix) == 0) out.push_back(dir + "/" + nm);
				}
				closedir(d);
			}
			std::sort(out.begin(), out.end());
			return out;
		}

		std::vector<std::string> pendingJobs(const std::string& dir) { return named(dir, ".job"); }

		std::vector<std::string> finishedJobs(const std::string& dir)
		{
			std::vector<std::string> v = named(dir, ".finished");
			std::reverse(v.begin(), v.end());
			return v;
		}

		std::string formatStatus(const Status& s)
		{
			char b[160];
			const std::string& st = s.state;
			if (st == "get") snprintf(b, sizeof b, "get %d %d %lld %lld %lld %lld", s.i, s.n, (long long)s.done, (long long)s.total, (long long)s.speed, (long long)s.eta);
			else if (st == "retry") snprintf(b, sizeof b, "retry %d %d %d %d %d", s.i, s.n, s.attempt, s.retries, s.seconds);
			else if (st == "unpack") snprintf(b, sizeof b, "unpack %d %d %d", s.i, s.n, s.percent);
			else if (st == "done") snprintf(b, sizeof b, "done %d %d %d %d", s.downloaded, s.unpacked, s.failed, s.packed);
			else snprintf(b, sizeof b, "%s %d %d", st.c_str(), s.i, s.n);
			auto clean = [](std::string v) { for (auto& c : v) if (c == '\n' || c == '\r' || c == '|') c = ' '; return v; };
			std::string why = s.why;
			for (auto& c : why) if (c == '\n' || c == '\r') c = ' ';
			return std::string(b) + "|" + clean(s.name) + "|" + why;
		}

		Status parseStatus(const std::string& line)
		{
			Status s;
			size_t bar = line.find('|');
			std::string head = line.substr(0, bar);
			if (bar != std::string::npos)
			{
				std::string rest = line.substr(bar + 1);
				size_t bar2 = rest.find('|');
				s.name = rest.substr(0, bar2);
				if (bar2 != std::string::npos) s.why = rest.substr(bar2 + 1);
			}
			while (!s.why.empty() && (s.why.back() == '\n' || s.why.back() == '\r')) s.why.pop_back();
			std::istringstream in(head);
			in >> s.state;
			long long a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
			if (s.state == "get") { in >> a >> b >> c >> d >> e >> f; s.i = (int)a; s.n = (int)b; s.done = c; s.total = d; s.speed = e; s.eta = f; }
			else if (s.state == "retry") { in >> a >> b >> c >> d >> e; s.i = (int)a; s.n = (int)b; s.attempt = (int)c; s.retries = (int)d; s.seconds = (int)e; }
			else if (s.state == "unpack") { in >> a >> b >> c; s.i = (int)a; s.n = (int)b; s.percent = (int)c; }
			else if (s.state == "done") { in >> a >> b >> c >> d; s.downloaded = (int)a; s.unpacked = (int)b; s.failed = (int)c; s.packed = (int)d; }
			else { in >> a >> b; s.i = (int)a; s.n = (int)b; }
			return s;
		}

		Status summary(const Job& j, const std::vector<Outcome>& outs)
		{
			Status s;
			s.state = "done";
			s.name = j.title;
			for (auto& o : outs)
			{
				if (o.index < 0 || o.index >= (int)j.items.size()) continue;
				if (!j.items[o.index].extract && o.what != "failed") s.downloaded++;
				if (o.what == "unpacked") s.unpacked++;
				if (o.what == "failed") s.failed++;
				if (o.what == "packed") s.packed++;
			}
			return s;
		}
	}
}
