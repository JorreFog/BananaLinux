#include "lang.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <mutex>
#include <set>
#include <unordered_map>

namespace banana::lang
{
	namespace
	{
		// a line of the English or of a translation in pieces: text, or a placeholder ({name}, or printf's %d %s %.1f...)
		struct Piece { std::string text; bool slot = false; bool named = false; };

		std::vector<Piece> pieces(const std::string& s)
		{
			std::vector<Piece> out;
			std::string lit;
			auto flush = [&] { if (!lit.empty()) { out.push_back({ lit }); lit.clear(); } };
			for (size_t i = 0; i < s.size();)
			{
				if (s[i] == '{')
				{
					size_t e = s.find('}', i);
					bool name = e != std::string::npos && e > i + 1;
					for (size_t k = i + 1; name && k < e; k++) name = (s[k] >= 'a' && s[k] <= 'z') || s[k] == '_';
					if (name) { flush(); out.push_back({ s.substr(i, e - i + 1), true, true }); i = e + 1; continue; }
				}
				if (s[i] == '%' && i + 1 < s.size())
				{
					if (s[i + 1] == '%') { lit += '%'; i += 2; continue; }
					size_t k = i + 1;
					while (k < s.size() && strchr("-+ #0", s[k])) k++;
					while (k < s.size() && isdigit((unsigned char)s[k])) k++;
					if (k < s.size() && s[k] == '.') { k++; while (k < s.size() && isdigit((unsigned char)s[k])) k++; }
					while (k < s.size() && strchr("lhzj", s[k])) k++;
					if (k < s.size() && strchr("dsuxXfgceip", s[k])) { flush(); out.push_back({ s.substr(i, k - i + 1), true, false }); i = k + 1; continue; }
				}
				lit += s[i++];
			}
			flush();
			return out;
		}

		struct Pattern { std::vector<Piece> en, tr; };

		// does text match the English's pieces? caps: what each placeholder stands for, in order
		bool match(const std::string& t, size_t at, const std::vector<Piece>& p, size_t k, std::vector<std::string>& caps)
		{
			if (k == p.size()) return at == t.size();
			if (!p[k].slot) return t.compare(at, p[k].text.size(), p[k].text) == 0 && match(t, at + p[k].text.size(), p, k + 1, caps);
			// a placeholder: one character at least, the shortest that lets the rest match
			for (size_t end = at + 1; end <= t.size(); end++)
			{
				caps.push_back(t.substr(at, end - at));
				if (match(t, end, p, k + 1, caps)) return true;
				caps.pop_back();
			}
			return false;
		}

		std::string fill(const Pattern& p, const std::vector<std::string>& caps)
		{
			// named ones by name, printf's in the order they come (the translator may move them, not swap two)
			std::vector<std::string> printfCaps;
			std::unordered_map<std::string, std::string> named;
			for (size_t i = 0, c = 0; i < p.en.size(); i++)
				if (p.en[i].slot) { if (p.en[i].named) named[p.en[i].text] = caps[c]; else printfCaps.push_back(caps[c]); c++; }
			std::string out;
			size_t next = 0;
			for (auto& x : p.tr)
			{
				if (!x.slot) out += x.text;
				else if (x.named) { auto it = named.find(x.text); out += it != named.end() ? it->second : x.text; }
				else out += next < printfCaps.size() ? printfCaps[next++] : x.text;
			}
			return out;
		}

		std::mutex mu;
		Info cur{ "en", "English", "English", "" };
		std::unordered_map<std::string, std::string> exact, inContext;	// English -> translation; context \x1f English
		// the English in lower case -> its translation ("" when two lines differing only in case translate apart): for
		// a line the code writes in capitals ("START") that the document has as "Start"
		std::unordered_map<std::string, std::string> folded;
		std::string lower(std::string s) { for (auto& c : s) if (c >= 'A' && c <= 'Z') c += 32; return s; }
		std::vector<Pattern> patterns;
		std::unordered_map<std::u32string, std::u32string> memo;
		std::string missingPath;
		std::set<std::string> missing;

		std::string to8(const std::u32string& s)
		{
			std::string o;
			for (char32_t c : s)
			{
				if (c < 0x80) o += (char)c;
				else if (c < 0x800) { o += (char)(0xc0 | c >> 6); o += (char)(0x80 | (c & 0x3f)); }
				else if (c < 0x10000) { o += (char)(0xe0 | c >> 12); o += (char)(0x80 | (c >> 6 & 0x3f)); o += (char)(0x80 | (c & 0x3f)); }
				else { o += (char)(0xf0 | c >> 18); o += (char)(0x80 | (c >> 12 & 0x3f)); o += (char)(0x80 | (c >> 6 & 0x3f)); o += (char)(0x80 | (c & 0x3f)); }
			}
			return o;
		}
		std::u32string to32(const std::string& s)
		{
			std::u32string o;
			for (size_t i = 0; i < s.size();)
			{
				unsigned char c = s[i];
				int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
				if (!n || i + n > s.size()) { i++; continue; }
				char32_t cp = n == 1 ? c : c & (0x7f >> n);
				for (int k = 1; k < n; k++) cp = cp << 6 | (s[i + k] & 0x3f);
				o += cp;
				i += n;
			}
			return o;
		}

		// "#: key = value" lines of a language file
		Info header(const std::string& path, const std::string& code)
		{
			Info in{ code, code, code, "" };
			std::ifstream f(path);
			std::string l;
			while (std::getline(f, l) && !l.empty() && l[0] == '#')
			{
				if (l.compare(0, 3, "#: ") != 0) continue;
				size_t eq = l.find(" = ");
				if (eq == std::string::npos) continue;
				std::string k = l.substr(3, eq - 3), v = l.substr(eq + 3);
				if (k == "language") in.name = v;
				else if (k == "language-en") in.nameEn = v;
				else if (k == "translated-by") in.by = v;
			}
			if (in.nameEn == code) in.nameEn = in.name;
			return in;
		}

		// the lookup, with the lock held; s: the text as the code made it
		std::string lookup(const std::string& s, bool& found)
		{
			found = true;
			auto it = exact.find(s);
			if (it != exact.end()) return it->second;
			// all in capitals, with no line of its own: the line in any case, when that is one translation
			bool upper = false, lowerSeen = false;
			for (unsigned char c : s) { if (c >= 'A' && c <= 'Z') upper = true; if (c >= 'a' && c <= 'z') lowerSeen = true; }
			if (upper && !lowerSeen)
			{
				auto f = folded.find(lower(s));
				if (f != folded.end() && !f->second.empty()) return f->second;
			}
			for (auto& p : patterns)
			{
				std::vector<std::string> caps;
				if (match(s, 0, p.en, 0, caps)) return fill(p, caps);
			}
			found = false;
			return s;
		}

		void noteMissing(const std::string& s)
		{
			// only text with letters in it (not a time, a number, a percent), once each, and not without end; and not a
			// piece of a line that was translated or cut short already (CJK, an ellipsis: lead bytes from 0xe2 on)
			bool letters = false;
			for (unsigned char c : s) if (c >= 0xe2 && c < 0xf8) return;
			for (unsigned char c : s) if ((c | 0x20) >= 'a' && (c | 0x20) <= 'z') { letters = true; break; }
			if (!letters || missingPath.empty() || missing.size() >= 3000 || !missing.insert(s).second) return;
			std::ofstream(missingPath, std::ios::app) << s << "\n";
		}
	}

	std::vector<Info> available(const std::string& dir)
	{
		std::vector<Info> out{ Info{ "en", "English", "English", "" } };
		std::vector<std::string> codes;
		if (DIR* d = opendir(dir.c_str()))
		{
			while (dirent* e = readdir(d))
			{
				std::string n = e->d_name;
				if (n.size() > 4 && n.compare(n.size() - 4, 4, ".tsv") == 0) codes.push_back(n.substr(0, n.size() - 4));
			}
			closedir(d);
		}
		std::sort(codes.begin(), codes.end());
		for (auto& c : codes) out.push_back(header(dir + "/" + c + ".tsv", c));
		return out;
	}

	bool load(const std::string& dir, const std::string& code, const std::vector<std::string>& parts)
	{
		std::lock_guard<std::mutex> l(mu);
		exact.clear(); inContext.clear(); folded.clear(); patterns.clear(); memo.clear(); missing.clear();
		cur = Info{ "en", "English", "English", "" };
		if (code.empty() || code == "en") return true;
		std::string path = dir + "/" + code + ".tsv";
		std::ifstream f(path);
		if (!f) return false;
		cur = header(path, code);
		std::set<std::string> want(parts.begin(), parts.end());
		std::string line;
		while (std::getline(f, line))
		{
			if (line.empty() || line[0] == '#') continue;
			size_t a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a + 1), c = b == std::string::npos ? b : line.find('\t', b + 1);
			if (c == std::string::npos) continue;
			std::string part = line.substr(0, a), ctx = line.substr(a + 1, b - a - 1), en = line.substr(b + 1, c - b - 1), tr = line.substr(c + 1);
			if (!want.count(part) || tr.empty()) continue;
			if (!ctx.empty()) { inContext.emplace(ctx + '\x1f' + en, tr); continue; }
			if (!exact.emplace(en, tr).second) continue;	// (the first part's, in the order the file has them)
			if (lower(tr) != lower(en))	// (a line kept as it is, "Nintendo DS", mustn't change a capitals one's case)
			{
				auto fl = folded.emplace(lower(en), tr);
				if (!fl.second && fl.first->second != tr) fl.first->second.clear();	// (two meanings: no guess)
			}
			std::vector<Piece> pe = pieces(en);
			bool slots = false;
			for (auto& p : pe) slots = slots || p.slot;
			if (slots) patterns.push_back({ pe, pieces(tr) });
		}
		// the most specific first: the longest fixed text
		auto fixed = [](const Pattern& p) { size_t n = 0; for (auto& x : p.en) if (!x.slot) n += x.text.size(); return n; };
		std::stable_sort(patterns.begin(), patterns.end(), [&](const Pattern& x, const Pattern& y) { return fixed(x) > fixed(y); });
		return true;
	}

	const Info& current() { return cur; }
	bool active() { return cur.code != "en"; }

	std::string tr(const std::string& text)
	{
		std::lock_guard<std::mutex> l(mu);
		if (cur.code == "en") return text;
		bool found;
		return lookup(text, found);
	}

	std::string tr(const std::string& context, const std::string& text)
	{
		{
			std::lock_guard<std::mutex> l(mu);
			auto it = inContext.find(context + '\x1f' + text);
			if (it != inContext.end()) return it->second;
		}
		return tr(text);
	}

	std::u32string tr32(const std::u32string& text)
	{
		std::lock_guard<std::mutex> l(mu);
		if (cur.code == "en" || text.empty()) return text;
		auto it = memo.find(text);
		if (it != memo.end()) return it->second;
		if (memo.size() > 20000) memo.clear();
		std::string s = to8(text);
		bool found;
		std::string t = lookup(s, found);
		if (!found) noteMissing(s);
		return memo[text] = found ? to32(t) : text;
	}

	void missingLog(const std::string& path)
	{
		std::lock_guard<std::mutex> l(mu);
		missingPath = path;
	}
}
