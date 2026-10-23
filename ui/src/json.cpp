#include "json.h"
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace banana
{
	const JVal* JVal::get(const std::string& key) const
	{
		if (kind != OBJ) return nullptr;
		for (auto& m : obj) if (m.first == key) return m.second.get();
		return nullptr;
	}
	std::string JVal::str(const std::string& key, const std::string& def) const
	{
		const JVal* v = get(key);
		if (!v) return def;
		if (v->kind == STR) return v->s;
		if (v->kind == NUM) { std::ostringstream o; o << v->n; return o.str(); }
		if (v->kind == BOOL) return v->b ? "true" : "false";
		return def;
	}
	double JVal::num(const std::string& key, double def) const
	{
		const JVal* v = get(key);
		if (!v) return def;
		if (v->kind == NUM) return v->n;
		if (v->kind == BOOL) return v->b;
		if (v->kind == STR) { char* e; double d = strtod(v->s.c_str(), &e); return e != v->s.c_str() ? d : def; }
		return def;
	}
	bool JVal::flag(const std::string& key, bool def) const
	{
		const JVal* v = get(key);
		if (!v) return def;
		if (v->kind == BOOL) return v->b;
		if (v->kind == NUM) return v->n != 0;
		if (v->kind == STR) return v->s == "true" || v->s == "on" || v->s == "yes";
		return def;
	}

	namespace
	{
		struct Parser
		{
			const std::string& s;
			size_t i = 0;
			std::string err;
			explicit Parser(const std::string& t) : s(t) {}

			bool fail(const std::string& m) { if (err.empty()) err = m; return false; }
			void ws()
			{
				for (;;)
				{
					while (i < s.size() && isspace((unsigned char)s[i])) i++;
					if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '/') { while (i < s.size() && s[i] != '\n') i++; continue; }
					if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '*')
					{
						size_t e = s.find("*/", i + 2);
						i = e == std::string::npos ? s.size() : e + 2;
						continue;
					}
					return;
				}
			}
			static void utf8(std::string& o, unsigned cp)
			{
				if (cp < 0x80) o += (char)cp;
				else if (cp < 0x800) { o += (char)(0xc0 | cp >> 6); o += (char)(0x80 | (cp & 63)); }
				else if (cp < 0x10000) { o += (char)(0xe0 | cp >> 12); o += (char)(0x80 | (cp >> 6 & 63)); o += (char)(0x80 | (cp & 63)); }
				else { o += (char)(0xf0 | cp >> 18); o += (char)(0x80 | (cp >> 12 & 63)); o += (char)(0x80 | (cp >> 6 & 63)); o += (char)(0x80 | (cp & 63)); }
			}
			bool string(std::string& out)
			{
				i++;	// "
				while (i < s.size() && s[i] != '"')
				{
					char c = s[i++];
					if (c != '\\') { out += c; continue; }
					if (i >= s.size()) break;
					char e = s[i++];
					switch (e)
					{
					case 'n': out += '\n'; break;
					case 't': out += '\t'; break;
					case 'r': out += '\r'; break;
					case 'b': out += '\b'; break;
					case 'f': out += '\f'; break;
					case 'u':
					{
						if (i + 4 > s.size()) return fail("a short \\u escape");
						unsigned cp = (unsigned)strtoul(s.substr(i, 4).c_str(), nullptr, 16);
						i += 4;
						if (cp >= 0xd800 && cp < 0xdc00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u')
						{
							unsigned lo = (unsigned)strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16);
							i += 6;
							cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
						}
						utf8(out, cp);
						break;
					}
					default: out += e;
					}
				}
				if (i >= s.size()) return fail("a string that doesn't end");
				i++;
				return true;
			}
			bool value(JPtr& out, int depth)
			{
				if (depth > 64) return fail("nested too deep");
				ws();
				if (i >= s.size()) return fail("the file ends where a value was expected");
				auto v = std::make_shared<JVal>();
				out = v;
				char c = s[i];
				if (c == '{')
				{
					v->kind = JVal::OBJ;
					i++;
					for (;;)
					{
						ws();
						if (i < s.size() && s[i] == '}') { i++; return true; }
						if (i >= s.size() || s[i] != '"') return fail("a member name (in quotes) expected");
						std::string k;
						if (!string(k)) return false;
						ws();
						if (i >= s.size() || s[i] != ':') return fail("':' expected after \"" + k + "\"");
						i++;
						JPtr m;
						if (!value(m, depth + 1)) return false;
						bool dup = false;
						for (auto& e : v->obj) if (e.first == k) { e.second = m; dup = true; }	// the last one wins
						if (!dup) v->obj.emplace_back(k, m);
						ws();
						if (i < s.size() && s[i] == ',') { i++; continue; }
						if (i < s.size() && s[i] == '}') { i++; return true; }
						return fail("',' or '}' expected");
					}
				}
				if (c == '[')
				{
					v->kind = JVal::ARR;
					i++;
					for (;;)
					{
						ws();
						if (i < s.size() && s[i] == ']') { i++; return true; }
						JPtr m;
						if (!value(m, depth + 1)) return false;
						v->arr.push_back(m);
						ws();
						if (i < s.size() && s[i] == ',') { i++; continue; }
						if (i < s.size() && s[i] == ']') { i++; return true; }
						return fail("',' or ']' expected");
					}
				}
				if (c == '"') { v->kind = JVal::STR; return string(v->s); }
				if (s.compare(i, 4, "true") == 0) { v->kind = JVal::BOOL; v->b = true; i += 4; return true; }
				if (s.compare(i, 5, "false") == 0) { v->kind = JVal::BOOL; i += 5; return true; }
				if (s.compare(i, 4, "null") == 0) { i += 4; return true; }
				char* e;
				double d = strtod(s.c_str() + i, &e);
				if (e == s.c_str() + i) return fail(std::string("unexpected '") + c + "'");
				v->kind = JVal::NUM; v->n = d;
				i = e - s.c_str();
				return true;
			}
		};
	}

	JPtr parseJson(const std::string& text, std::string* why)
	{
		Parser p(text);
		JPtr r;
		if (!p.value(r, 0) || (p.ws(), p.i < text.size() && p.err.empty() && !p.fail("more after the end")))
		{
			if (why)
			{
				int line = 1;
				for (size_t k = 0; k < p.i && k < text.size(); k++) if (text[k] == '\n') line++;
				*why = p.err + " (line " + std::to_string(line) + ")";
			}
			return nullptr;
		}
		return r;
	}

	JPtr readJsonFile(const std::string& path, std::string* why)
	{
		std::ifstream f(path);
		if (!f) { if (why) *why = "can't open " + path; return nullptr; }
		std::stringstream ss; ss << f.rdbuf();
		JPtr r = parseJson(ss.str(), why);
		if (!r && why) *why = path + ": " + *why;
		return r;
	}
}
