// banana-ui: the game companion's definitions, matching and reading (gamecomp.h). The companion idea is PokeDaisy's
// (Lidor Itzhari).
#include "gamecomp.h"
#include "zip.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>
#include <zlib.h>

namespace banana::gcomp
{
	namespace
	{
		const char* const PAGE_IDS[NUM_PAGES] = { "status", "detail", "battle", "map", "bag", "collection", "progress" };
		// the Pokémon companion's games (pokemon.h detectGame: Ruby, Sapphire, Emerald, FireRed, LeafGreen and the hacks
		// built on them, which keep the code): never the generic companion
		const char* const POKEMON_CODES[] = { "AXVE", "AXPE", "BPEE", "BPRE", "BPGE" };
		const uint32_t ROM_BASE = 0x08000000, ROM_END = 0x0a000000;

		// what can be read: EWRAM, IWRAM (RetroArch, live) and the ROM (the file)
		bool inRam(uint32_t a, int n) { uint64_t e = (uint64_t)a + (uint64_t)std::max(n, 1);
			return (a >= 0x02000000 && e <= 0x02040000) || (a >= 0x03000000 && e <= 0x03008000); }
		bool inRom(uint32_t a, int n) { uint64_t e = (uint64_t)a + (uint64_t)std::max(n, 1); return a >= ROM_BASE && e <= ROM_END; }
		uint32_t ramEnd(uint32_t a) { return a < 0x03000000 ? 0x02040000 : 0x03008000; }

		bool parseInt(const JVal* v, long long& out)
		{
			if (!v) return false;
			if (v->kind == JVal::NUM) { out = (long long)v->n; return true; }
			if (v->kind != JVal::STR) return false;
			std::string s;
			for (char c : v->s) if (c != '_' && !isspace((unsigned char)c)) s += c;
			if (s.empty()) return false;
			bool neg = s[0] == '-';
			if (neg) s.erase(0, 1);
			char* e;
			long long r = (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) ? strtoll(s.c_str() + 2, &e, 16) : strtoll(s.c_str(), &e, 10);
			if (*e) return false;
			out = neg ? -r : r;
			return true;
		}

		// the names an expression uses (not the functions it calls, not what is in quotes)
		void exprNames(const std::string& t, std::vector<std::string>& out)
		{
			for (size_t i = 0; i < t.size();)
			{
				char c = t[i];
				if (c == '\'' || c == '"')
				{
					for (i++; i < t.size() && t[i] != c; i++) if (t[i] == '\\') i++;
					i++;
					continue;
				}
				if (isdigit((unsigned char)c))
				{
					while (i < t.size() && (isalnum((unsigned char)t[i]) || t[i] == '.')) i++;
					continue;
				}
				if (isalpha((unsigned char)c) || c == '_' || c == '$')
				{
					size_t b = i;
					while (i < t.size() && (isalnum((unsigned char)t[i]) || t[i] == '_' || t[i] == '.' || t[i] == '$')) i++;
					std::string id = t.substr(b, i - b);
					size_t j = i;
					while (j < t.size() && isspace((unsigned char)t[j])) j++;
					if (j < t.size() && t[j] == '(') continue;
					if (id == "true" || id == "false" || id == "null") continue;
					if (id[0] == '$') id.erase(0, 1);
					if (std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
					continue;
				}
				i++;
			}
		}

		bool parseNum(const JVal* v, NumRef& r, std::string* why, const char* what)
		{
			if (!v) return true;
			if (v->kind == JVal::NUM || v->kind == JVal::BOOL) { r.set = true; r.lit = v->kind == JVal::NUM ? v->n : v->b; return true; }
			if (v->kind != JVal::STR) { if (why) *why = std::string(what) + ": not a number or an expression"; return false; }
			std::string e;
			r.expr = compileExpr(v->s, &e);
			if (!r.expr) { if (why) *why = std::string(what) + ": " + e; return false; }
			exprNames(v->s, r.deps);
			r.set = true;
			return true;
		}

		bool parseMem(const JVal& j, Mem& m, bool field, std::string* why, const std::string& where)
		{
			static const char* const T[] = { "u8", "u16", "u32", "s8", "s16", "s32", "bcd8", "bcd16", "bcd32", "bool", "text", "index" };
			static const int SZ[] = { 1, 2, 4, 1, 2, 4, 1, 2, 4, 1, 0, 0 };
			std::string t = j.str("type", "u8");
			int ti = -1;
			for (int i = 0; i < 12; i++) if (t == T[i]) ti = i;
			if (ti < 0 || (ti == INDEX && !field)) { if (why) *why = where + ": type " + t; return false; }
			m.type = (VType)ti;
			m.size = SZ[ti];
			if (m.type == TEXT)
			{
				m.size = std::max(1, std::min(64, (int)j.num("len", 16)));
				m.term = (int)j.num("term", 0);
				std::string enc = j.str("enc", "ascii");
				if (enc.compare(0, 8, "charmap:") == 0) m.charmap = enc.substr(8);
			}
			long long v;
			if (!field)
			{
				if (const JVal* p = j.get("ptr"))
				{
					long long pa, po;
					if (!p->isArr() || p->arr.size() != 2 || !parseInt(p->arr[0].get(), pa) || !parseInt(p->arr[1].get(), po))
					{ if (why) *why = where + ": ptr"; return false; }
					m.ptr = true; m.ptrAddr = (uint32_t)pa; m.ptrOff = (int32_t)po;
					if (!inRam(m.ptrAddr, 4) && !inRom(m.ptrAddr, 4)) { if (why) *why = where + ": ptr address not readable"; return false; }
				}
				else
				{
					if (!parseInt(j.get("addr"), v)) { if (why) *why = where + ": addr"; return false; }
					m.addr = (uint32_t)v;
					if (!inRam(m.addr, m.size) && !inRom(m.addr, m.size)) { if (why) *why = where + ": address not readable"; return false; }
				}
			}
			if (parseInt(j.get("mask"), v)) m.mask = (uint32_t)v;
			if (parseInt(j.get("shift"), v)) m.shift = (int)std::max(0LL, std::min(31LL, v));
			if (const JVal* x = j.get("mul")) if (x->isNum()) m.mul = x->n;
			if (parseInt(j.get("div"), v) && v != 0) m.div = (long)v;
			if (parseInt(j.get("add"), v)) m.add = (long)v;
			return true;
		}

		uint32_t le(const uint8_t* p, int n) { uint32_t r = 0; for (int i = n - 1; i >= 0; i--) r = r << 8 | p[i]; return r; }

		// the bytes at p as m says
		EV decode(const Mem& m, const uint8_t* p, const Def& d)
		{
			if (m.type == TEXT)
			{
				std::string s;
				const std::map<long, std::string>* cm = nullptr;
				if (!m.charmap.empty()) { auto it = d.enums.find(m.charmap); if (it != d.enums.end()) cm = &it->second; }
				for (int i = 0; i < m.size; i++)
				{
					if (m.term >= 0 && p[i] == m.term) break;
					if (cm) { auto it = cm->find(p[i]); s += it != cm->end() ? it->second : "?"; }
					else if (p[i] >= 0x20 && p[i] < 0x7f) s += (char)p[i];
				}
				while (!s.empty() && s.back() == ' ') s.pop_back();
				return EV(s);
			}
			uint32_t raw = le(p, m.size);
			double v;
			if (m.type >= BCD8 && m.type <= BCD32)
			{
				uint64_t r = 0, k = 1;
				for (int i = 0; i < m.size * 2; i++) { r += ((raw >> (4 * i)) & 0xf) % 10 * k; k *= 10; }
				raw = (uint32_t)r;
			}
			if (m.mask != 0xffffffffu || m.type == U8 || m.type == U16 || m.type == U32 || m.type == BOOL || (m.type >= BCD8 && m.type <= BCD32))
				v = (double)((raw & m.mask) >> m.shift);
			else
			{
				int32_t s = m.type == S8 ? (int8_t)raw : m.type == S16 ? (int16_t)raw : (int32_t)raw;
				v = (double)(s >> m.shift);
			}
			if (m.type == BOOL) v = v != 0;
			v *= m.mul;
			if (m.div != 1) v = std::trunc(v / m.div);
			v += m.add;
			return EV(v);
		}

		std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
		bool endsWithI(const std::string& s, const char* suf)
		{
			size_t n = strlen(suf);
			return s.size() >= n && lower(s.substr(s.size() - n)) == suf;
		}
		std::string trimTitle(const uint8_t* p, size_t n)
		{
			std::string s((const char*)p, n);
			while (!s.empty() && (s.back() == 0 || s.back() == ' ')) s.pop_back();
			return s;
		}
		std::vector<std::string> split(const std::string& s, char c)
		{
			std::vector<std::string> r;
			std::string cur;
			std::istringstream in(s);
			while (std::getline(in, cur, c)) if (!cur.empty()) r.push_back(cur);
			return r;
		}

		// the whole ROM (a .gba, or the .gba in a .zip), up to 32 MB
		std::vector<uint8_t> readRom(const std::string& path, size_t limit)
		{
			std::vector<uint8_t> out;
			if (endsWithI(path, ".zip"))
			{
				readZip(path, [](const std::vector<rnds::ZipMember>& ms)
				{
					for (size_t i = 0; i < ms.size(); i++) if (endsWithI(ms[i].name, ".gba")) return (int)i;
					return ms.empty() ? -1 : 0;
				}, [&](const uint8_t* p, size_t n)
				{
					out.insert(out.end(), p, p + std::min(n, limit - out.size()));
					return out.size() < limit;
				});
				return out;
			}
			FILE* f = fopen(path.c_str(), "rb");
			if (!f) return out;
			out.resize(limit);
			out.resize(fread(out.data(), 1, limit, f));
			fclose(f);
			return out;
		}
	}

	const char* pageIdName(int p) { return p >= 0 && p < NUM_PAGES ? PAGE_IDS[p] : ""; }

	std::string Def::enumLabel(const std::string& e, long v) const
	{
		auto it = enums.find(e);
		if (it != enums.end()) { auto l = it->second.find(v); if (l != it->second.end()) return l->second; }
		return std::to_string(v);
	}

	std::shared_ptr<Def> parseDef(const JVal& j, std::string* why)
	{
		auto d = std::make_shared<Def>();
		if (!j.isObj() || j.num("schema", 0) != 1) { if (why) *why = "not a schema 1 definition"; return nullptr; }
		d->title = j.str("title");
		d->quality = j.str("quality");
		uint32_t c;
		if (parseColorText(j.str("accent", "#d0a030"), c)) d->accent = c & 0xffffff;
		if (const JVal* en = j.get("enums"))
			for (auto& e : en->obj)
				for (auto& kv : e.second->obj)
				{
					JVal k; k.kind = JVal::STR; k.s = kv.first;
					long long n;
					if (parseInt(&k, n) && kv.second->isStr()) d->enums[e.first][(long)n] = kv.second->s;
				}
		if (const JVal* vs = j.get("vars"))
			for (auto& e : vs->obj)
			{
				Var v;
				v.name = e.first;
				const JVal& x = *e.second;
				if (const JVal* ex = x.get("expr"))
				{
					std::string w;
					v.kind = Var::EXPR;
					v.expr = ex->isStr() ? compileExpr(ex->s, &w) : nullptr;
					if (!v.expr) { if (why) *why = "vars." + e.first + ": " + w; return nullptr; }
					exprNames(ex->s, v.deps);
				}
				else if (const JVal* cv = x.get("value"))
				{
					v.kind = Var::CONST;
					v.value = cv->isStr() ? EV(cv->s) : EV(cv->n);
				}
				else
				{
					v.kind = Var::MEM;
					if (!parseMem(x, v.m, false, why, "vars." + e.first)) return nullptr;
					if (v.m.ptr ? inRom(v.m.ptrAddr, 4) : inRom(v.m.addr, v.m.size)) d->romData = true;	// (a pointer into the ROM: read when met)
				}
				d->varIndex[v.name] = (int)d->vars.size();
				d->vars.push_back(v);
			}
		const JVal* ps = j.get("pages");
		if (!ps || !ps->isArr()) { if (why) *why = "no pages"; return nullptr; }
		bool have[NUM_PAGES] = {};
		for (auto& pp : ps->arr)
		{
			int id = -1;
			for (int i = 0; i < NUM_PAGES; i++) if (pp->str("id") == PAGE_IDS[i]) id = i;
			if (id < 0) continue;
			have[id] = true;
			Page& p = d->pages[id];
			p.title = pp->str("title", PAGE_IDS[id]);
			p.tab = pp->str("tab");
			if (!parseNum(pp->get("when"), p.when, why, "when")) return nullptr;
			bool live = false;
			const JVal* ws = pp->get("widgets");
			if (ws && ws->isArr())
				for (auto& wj : ws->arr)
				{
					static const char* const WT[] = { "number", "text", "bar", "flags", "count", "list", "static", "grid", "kv" };
					Widget w;
					int t = -1;
					for (int i = 0; i < 9; i++) if (wj->str("type") == WT[i]) t = i;
					if (t < 0) continue;
					w.type = (Widget::Type)t;
					w.label = wj->str("label");
					w.text = wj->str("text");
					if (w.label.empty() && t == Widget::STATIC) w.label = wj->str("title");
					if (t == Widget::LIST) w.label = wj->str("title", w.label);
					w.enumName = wj->str("enum");
					w.format = wj->str("format");
					std::string where = std::string(PAGE_IDS[id]) + "." + WT[t];
					if (!parseNum(wj->get("value"), w.value, why, (where + ".value").c_str()) || !parseNum(wj->get("max"), w.max, why, "max") ||
						!parseNum(wj->get("total"), w.total, why, "total") || !parseNum(wj->get("when"), w.when, why, "when") ||
						!parseNum(wj->get("rowWhen"), w.rowWhen, why, "rowWhen")) return nullptr;
					if (wj->get("color") && parseColorText(wj->str("color"), c)) w.color = c & 0xffffff;
					w.invert = wj->flag("invert");
					w.bits = wj->flag("bits");
					if (const JVal* it = wj->get("items")) for (auto& s : it->arr) w.items.push_back(s->isStr() ? s->s : "");
					if (const JVal* rs = wj->get("rows"))
						for (auto& r : rs->arr)
							if (r->isArr() && r->arr.size() == 2) w.rows.push_back({ r->arr[0]->s, r->arr[1]->s });
					if (t == Widget::LIST)
					{
						long long v;
						const JVal* b = wj->get("base");
						if (b && b->isObj() && b->get("ptr"))
						{
							const JVal* p = b->get("ptr");
							long long pa, po;
							if (!p->isArr() || p->arr.size() != 2 || !parseInt(p->arr[0].get(), pa) || !parseInt(p->arr[1].get(), po))
							{ if (why) *why = where + ": base ptr"; return nullptr; }
							w.basePtr = true; w.ptrAddr = (uint32_t)pa; w.ptrOff = (int32_t)po;
							if (inRom(w.ptrAddr, 4)) d->romData = true;
						}
						else if (parseInt(b, v)) w.base = (uint32_t)v;
						else { if (why) *why = where + ": base"; return nullptr; }
						w.stride = (int)wj->num("stride", 0);
						if (w.stride <= 0) { if (why) *why = where + ": stride"; return nullptr; }
						if (!parseNum(wj->get("n"), w.n, why, "n")) return nullptr;
						if (!w.n.set) { w.n.set = true; w.n.lit = 1; }
						if (!w.basePtr && inRom(w.base, 1)) d->romData = true;
						if (const JVal* fs = wj->get("fields"))
							for (auto& fj : fs->arr)
							{
								Field f;
								f.name = fj->str("name");
								f.label = fj->str("label", f.name);
								f.enumName = fj->str("enum");
								f.format = fj->str("format");
								long long o = 0;
								parseInt(fj->get("off"), o);
								f.off = (uint32_t)o;
								if (!parseMem(*fj, f.m, true, why, where + ".fields." + f.name)) return nullptr;
								if ((int)f.off + f.m.size > w.stride) { if (why) *why = where + ".fields." + f.name + ": past the stride"; return nullptr; }
								w.fields.push_back(f);
							}
						if (w.fields.empty()) { if (why) *why = where + ": no fields"; return nullptr; }
						w.listId = d->lists++;
						live = live || w.basePtr || !inRom(w.base, 1);
					}
					p.widgets.push_back(w);
				}
			if (p.widgets.empty()) { if (why) *why = std::string("page ") + PAGE_IDS[id] + ": no widgets"; return nullptr; }
			// a reference page: nothing on it read from the game's memory (unless the file says)
			std::vector<std::string> names;
			for (auto& w : p.widgets)
				for (const NumRef* r : { &w.value, &w.max, &w.total, &w.n })
					names.insert(names.end(), r->deps.begin(), r->deps.end());
			// (a name is live when it, or what its expression uses, reads RAM)
			std::vector<std::string> todo = names;
			std::vector<std::string> seen;
			while (!todo.empty() && !live)
			{
				std::string n = todo.back(); todo.pop_back();
				if (std::find(seen.begin(), seen.end(), n) != seen.end()) continue;
				seen.push_back(n);
				const Var* v = d->var(n);
				if (!v) continue;
				if (v->kind == Var::MEM && (v->m.ptr || !inRom(v->m.addr, v->m.size))) live = true;
				todo.insert(todo.end(), v->deps.begin(), v->deps.end());
			}
			const JVal* ref = pp->get("reference");
			p.reference = ref && ref->kind == JVal::BOOL ? ref->b : !live;
		}
		for (int i = 0; i < NUM_PAGES; i++)
			if (!have[i]) { if (why) *why = std::string("no page ") + PAGE_IDS[i]; return nullptr; }
		return d;
	}

	std::shared_ptr<Def> loadDef(const std::string& path, std::string* why)
	{
		JPtr j = readJsonFile(path, why);
		if (!j) return nullptr;
		auto d = parseDef(*j, why);
		if (d) d->file = path;
		return d;
	}

	bool romId(const std::string& path, RomId& id, bool computeCrc)
	{
		std::vector<uint8_t> h;
		id = RomId();
		size_t slash = path.find_last_of('/');
		id.base = path.substr(slash == std::string::npos ? 0 : slash + 1);
		size_t dot = id.base.find_last_of('.');
		if (dot != std::string::npos) id.base.erase(dot);
		if (endsWithI(path, ".zip"))
		{
			readZip(path, [&](const std::vector<rnds::ZipMember>& ms)
			{
				int k = -1;
				for (size_t i = 0; i < ms.size(); i++) if (endsWithI(ms[i].name, ".gba")) { k = (int)i; break; }
				if (k < 0 && !ms.empty()) k = 0;
				if (k >= 0) { id.crc = ms[k].crc; id.crcKnown = true; }
				return k;
			}, [&](const uint8_t* p, size_t n)
			{
				h.insert(h.end(), p, p + std::min(n, (size_t)0xc0 - h.size()));
				return h.size() < 0xc0;
			});
		}
		else
		{
			FILE* f = fopen(path.c_str(), "rb");
			if (!f) return false;
			h.resize(0xc0);
			h.resize(fread(h.data(), 1, h.size(), f));
			if (computeCrc)
			{
				uLong crc = crc32(0L, Z_NULL, 0);
				std::vector<uint8_t> b(1 << 20);
				fseek(f, 0, SEEK_SET);
				size_t n;
				while ((n = fread(b.data(), 1, b.size(), f)) > 0) crc = crc32(crc, b.data(), (uInt)n);
				id.crc = (uint32_t)crc; id.crcKnown = true;
			}
			fclose(f);
		}
		if (h.size() < 0xb0) return false;
		id.title = trimTitle(h.data() + 0xa0, 12);
		id.code = std::string((const char*)h.data() + 0xac, 4);
		return true;
	}

	namespace
	{
		struct IndexLine { std::string file, title12, zip; std::vector<std::string> codes; std::vector<uint32_t> crcs; bool exclusive = false; };
		bool indexOf(const JVal& j, const std::string& file, IndexLine& l)
		{
			const JVal* m = j.get("match");
			if (!m || j.num("schema", 0) != 1) return false;
			l.file = file;
			l.title12 = m->str("title12");
			l.zip = m->str("zip");
			l.exclusive = m->flag("exclusive");
			if (const JVal* c = m->get("code")) { if (c->isStr()) l.codes.push_back(c->s); else for (auto& x : c->arr) l.codes.push_back(x->s); }
			if (const JVal* c = m->get("crc32"))
			{
				std::vector<std::string> cs;
				if (c->isStr()) cs.push_back(c->s); else for (auto& x : c->arr) cs.push_back(x->s);
				for (auto& s : cs) l.crcs.push_back((uint32_t)strtoul(s.c_str(), nullptr, 16));
			}
			return true;
		}
		std::vector<IndexLine> readIndex(const std::string& dir)
		{
			std::vector<IndexLine> out;
			std::ifstream f(dir + "/index.tsv");
			std::string ln;
			if (f)
			{
				// file, codes (comma), title12, crc32s (comma), exclusive (0/1), zip
				while (std::getline(f, ln))
				{
					if (ln.empty() || ln[0] == '#') continue;
					std::vector<std::string> c;
					size_t a = 0, b;
					while ((b = ln.find('\t', a)) != std::string::npos) { c.push_back(ln.substr(a, b - a)); a = b + 1; }
					c.push_back(ln.substr(a));
					if (c.size() < 6) continue;
					IndexLine l;
					l.file = c[0]; l.codes = split(c[1], ','); l.title12 = c[2];
					for (auto& s : split(c[3], ',')) l.crcs.push_back((uint32_t)strtoul(s.c_str(), nullptr, 16));
					l.exclusive = c[4] == "1"; l.zip = c[5];
					out.push_back(l);
				}
				return out;
			}
			DIR* d = opendir(dir.c_str());
			if (!d) return out;
			std::vector<std::string> names;
			while (dirent* e = readdir(d)) { std::string n = e->d_name; if (endsWithI(n, ".json")) names.push_back(n); }
			closedir(d);
			std::sort(names.begin(), names.end());
			for (auto& n : names)
			{
				JPtr j = readJsonFile(dir + "/" + n);
				IndexLine l;
				if (j && indexOf(*j, n, l)) out.push_back(l);
			}
			return out;
		}
	}

	std::string makeIndex(const std::string& dir)
	{
		// (straight from the files, never from an index.tsv that is there)
		std::string out = "# file\tcodes\ttitle12\tcrc32s\texclusive\tzip (banana-ui's game companion, gamecomp.h)\n";
		DIR* d = opendir(dir.c_str());
		if (!d) return out;
		std::vector<std::string> names;
		while (dirent* e = readdir(d)) { std::string n = e->d_name; if (endsWithI(n, ".json")) names.push_back(n); }
		closedir(d);
		std::sort(names.begin(), names.end());
		for (auto& n : names)
		{
			JPtr j = readJsonFile(dir + "/" + n);
			IndexLine l;
			if (!j || !indexOf(*j, n, l)) continue;
			std::string codes, crcs;
			for (auto& c : l.codes) codes += (codes.empty() ? "" : ",") + c;
			for (uint32_t c : l.crcs) { char b[16]; snprintf(b, sizeof b, "%s%08x", crcs.empty() ? "" : ",", c); crcs += b; }
			out += n + "\t" + codes + "\t" + l.title12 + "\t" + crcs + "\t" + (l.exclusive ? "1" : "0") + "\t" + l.zip + "\n";
		}
		return out;
	}

	std::string findDef(const std::string& dir, const std::string& rom, std::string* why)
	{
		std::vector<IndexLine> idx = readIndex(dir);
		if (idx.empty()) { if (why) *why = "no definitions in " + dir; return ""; }
		RomId id;
		if (!romId(rom, id, false)) { if (why) *why = "no ROM header"; return ""; }
		for (const char* pc : POKEMON_CODES) if (id.code == pc) { if (why) *why = "a Pokémon game (the Pokémon companion's)"; return ""; }
		// a .gba's CRC-32 is worked out (the whole file read) only when a hack or translation could be this ROM: an
		// exclusive definition with this header's code, or with no code
		if (!id.crcKnown)
			for (auto& l : idx)
				if (l.exclusive && !l.crcs.empty() && (l.codes.empty() || std::find(l.codes.begin(), l.codes.end(), id.code) != l.codes.end()))
				{ romId(rom, id, true); break; }
		if (id.crcKnown)
			for (auto& l : idx)
				if (std::find(l.crcs.begin(), l.crcs.end(), id.crc) != l.crcs.end()) return dir + "/" + l.file;
		const IndexLine* byCode = nullptr;
		for (auto& l : idx)
		{
			if (l.exclusive || std::find(l.codes.begin(), l.codes.end(), id.code) == l.codes.end()) continue;
			std::string t = l.title12;
			while (!t.empty() && t.back() == ' ') t.pop_back();	// (the header's is without its padding)
			if (!t.empty() && t != id.title) continue;
			byCode = &l;
			break;
		}
		if (byCode) return dir + "/" + byCode->file;
		for (auto& l : idx)
		{
			std::string z = l.zip;
			size_t dot = z.find_last_of('.');
			if (dot != std::string::npos) z.erase(dot);
			if (!l.exclusive && !z.empty() && lower(z) == lower(id.base)) return dir + "/" + l.file;
		}
		if (why) *why = "no definition for " + id.code + " \"" + id.title + "\"";
		return "";
	}

	// ---- the engine --------------------------------------------------------------------------------------------------

	bool Engine::open(const std::string& rom, const std::string& dir)
	{
		close();
		std::string why;
		std::string f = findDef(dir, rom, &why);
		if (f.empty()) { fprintf(stderr, "game companion: %s\n", why.c_str()); return false; }
		auto d = loadDef(f, &why);
		if (!d) { fprintf(stderr, "game companion: %s: %s\n", f.c_str(), why.c_str()); return false; }
		return openDef(d, rom);
	}

	bool Engine::openDef(std::shared_ptr<const Def> d, const std::string& rom)
	{
		close();
		def = d;
		romPath = rom;
		rs = std::make_shared<RomShared>();
		snap.lists.assign(def->lists, {});
		if (def->romData) startRom();
		return true;
	}

	void Engine::close()
	{
		def.reset();
		rs.reset();
		romPath.clear();
		wantPage = P_STATUS; wantList = -1;
		nextAt = sentAt = 0;
		waiting = lastReads = 0;
		part.clear(); err.clear();
		snap = Snapshot();
		chunks.clear();
		ptrSeen.clear(); ptrUsed.clear();
		changed = false;
	}

	void Engine::startRom()
	{
		if (!rs || romPath.empty()) return;
		{
			std::lock_guard<std::mutex> l(rs->mu);
			if (rs->started) return;
			rs->started = true;
		}
		std::shared_ptr<RomShared> s = rs;
		std::string path = romPath;
		std::thread([s, path]
		{
			setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 10);	// (beside the game starting)
			auto r = std::make_shared<const std::vector<uint8_t>>(readRom(path, 32u << 20));
			std::lock_guard<std::mutex> l(s->mu);
			s->rom = r;
		}).detach();
	}

	std::shared_ptr<const std::vector<uint8_t>> Engine::romBytes() const
	{
		if (!rs) return nullptr;
		std::lock_guard<std::mutex> l(rs->mu);
		return rs->rom;
	}

	const uint8_t* Engine::at(uint32_t a, int n) const
	{
		if (inRom(a, n))
		{
			auto r = romBytes();	// (held by Engine's rs as long as the engine is open)
			if (!r || (size_t)(a - ROM_BASE) + n > r->size()) return nullptr;
			return r->data() + (a - ROM_BASE);
		}
		for (auto& c : chunks)
			if (a >= c.addr && (uint64_t)a + n <= (uint64_t)c.addr + c.bytes.size()) return c.bytes.data() + (a - c.addr);
		return nullptr;
	}

	void Engine::neededVars(std::vector<bool>& need) const
	{
		need.assign(def->vars.size(), false);
		std::vector<std::string> todo;
		auto add = [&](const NumRef& r) { todo.insert(todo.end(), r.deps.begin(), r.deps.end()); };
		for (int p = 0; p < NUM_PAGES; p++) add(def->pages[p].when);
		if (wantPage >= 0 && wantPage < NUM_PAGES)
			for (auto& w : def->pages[wantPage].widgets)
				for (const NumRef* r : { &w.value, &w.max, &w.total, &w.n, &w.when, &w.rowWhen }) add(*r);
		while (!todo.empty())
		{
			auto it = def->varIndex.find(todo.back());
			todo.pop_back();
			if (it == def->varIndex.end() || need[it->second]) continue;
			need[it->second] = true;
			const Var& v = def->vars[it->second];
			todo.insert(todo.end(), v.deps.begin(), v.deps.end());
		}
	}

	std::string Engine::requests(double now)
	{
		if (!def) return "";
		if (now < nextAt || (waiting > 0 && now - sentAt < 10000)) return "";
		std::vector<bool> need;
		neededVars(need);
		struct R { uint32_t a, e; };
		std::vector<R> want;
		ptrUsed.clear();
		auto ram = [&](uint32_t a, int n) { if (inRam(a, n)) want.push_back({ a, a + (uint32_t)n }); };
		for (size_t i = 0; i < def->vars.size(); i++)
		{
			const Var& v = def->vars[i];
			if (!need[i] || v.kind != Var::MEM) continue;
			if (!v.m.ptr) { ram(v.m.addr, v.m.size); continue; }
			ram(v.m.ptrAddr, 4);
			auto s = ptrSeen.find(v.name);
			if (s != ptrSeen.end())
			{
				uint32_t t = s->second + (uint32_t)v.m.ptrOff;
				ptrUsed[v.name] = t;
				ram(t, v.m.size);
				if (inRom(t, v.m.size)) startRom();
			}
		}
		if (wantPage >= 0 && wantPage < NUM_PAGES)
			for (auto& w : def->pages[wantPage].widgets)
			{
				if (w.type != Widget::LIST) continue;
				int rows = w.n.expr ? 64 : std::max(0, std::min(64, (int)w.n.lit));
				rows = std::max(1, std::min(rows, 4096 / w.stride));
				uint32_t b = w.base;
				std::string key = "#list" + std::to_string(w.listId);
				if (w.basePtr)
				{
					ram(w.ptrAddr, 4);
					auto s = ptrSeen.find(key);
					if (s == ptrSeen.end()) continue;
					b = s->second + (uint32_t)w.ptrOff;
					ptrUsed[key] = b;
					if (inRom(b, 1)) startRom();
				}
				if (inRam(b, 1))
				{
					uint32_t e = std::min<uint64_t>((uint64_t)b + (uint64_t)rows * w.stride, ramEnd(b));
					want.push_back({ b, e });
				}
			}
		// coalesced: sorted, neighbours within 512 bytes (in the same memory) share a read of at most 4 KB (a longer
		// table alone is one read of its own)
		std::sort(want.begin(), want.end(), [](const R& x, const R& y) { return x.a < y.a; });
		chunks.clear();
		for (auto& r : want)
		{
			if (!chunks.empty())
			{
				Chunk& c = chunks.back();
				uint32_t ce = c.addr + (uint32_t)c.len;
				if (r.a <= ce + 512 && ramEnd(r.a) == ramEnd(c.addr) && (r.e <= ce || r.e - c.addr <= 4096))
				{
					c.len = (int)(std::max(ce, r.e) - c.addr);
					continue;
				}
			}
			chunks.push_back({ r.a, (int)(r.e - r.a), {}, false });
		}
		lastReads = (int)chunks.size();
		sentAt = now;
		nextAt = now + 250;
		if (chunks.empty())
		{
			// nothing from RetroArch (a reference page, ROM values): worked out now, again each second (the ROM's
			// bytes come on a thread)
			finish();
			nextAt = now + 1000;
			waiting = 0;
			return "";
		}
		std::string out;
		char b[64];
		for (auto& c : chunks) { snprintf(b, sizeof b, "READ_CORE_MEMORY %x %d\n", c.addr, c.len); out += b; }
		waiting = (int)chunks.size();
		return out;
	}

	void Engine::feed(const char* data, size_t n)
	{
		part.append(data, n);
		size_t nl;
		static const char P[] = "READ_CORE_MEMORY ";
		while ((nl = part.find('\n')) != std::string::npos)
		{
			std::string ln = part.substr(0, nl);
			part.erase(0, nl + 1);
			if (!def || ln.compare(0, sizeof P - 1, P) != 0) continue;
			const char* s = ln.c_str() + sizeof P - 1;
			char* e;
			unsigned long a = strtoul(s, &e, 16);
			if (e == s) continue;
			std::vector<uint8_t> v;
			bool error = false;
			s = e;
			while (*s == ' ') s++;
			if (*s == '-') error = true;	// "-1 no memory map defined"
			else
				while (*s)
				{
					unsigned long x = strtoul(s, &e, 16);
					if (e == s || x > 0xff) break;
					v.push_back((uint8_t)x);
					s = e;
					while (*s == ' ') s++;
				}
			auto c = std::find_if(chunks.begin(), chunks.end(), [&](const Chunk& x) { return x.addr == (uint32_t)a && !x.done; });
			if (c == chunks.end()) continue;
			if (error && err.empty()) { size_t at = ln.find("-1"); err = ln.substr(std::min(ln.size(), at + 3)); }
			c->bytes = v;
			c->done = true;
			if (waiting > 0 && --waiting == 0) finish();
		}
		if (part.size() > 1 << 20) part.clear();	// (a line with no end: not ours)
	}

	Val Engine::evalIn(const NumRef& r, const Scope& sc, const std::map<std::string, Val>& vals) const
	{
		Val v;
		if (!r.set) return v;
		if (!r.expr) { v.ok = true; v.v = EV(r.lit); return v; }
		for (auto& n : r.deps)
		{
			EV x;
			if (!sc.lookup(n, x)) { auto it = vals.find(n); if (it == vals.end() || !it->second.ok) return v; }
		}
		v.v = evalExpr(r.expr, sc);
		v.ok = true;
		return v;
	}

	Val Engine::eval(const NumRef& r) const
	{
		Scope sc;
		for (auto& kv : snap.vals) if (kv.second.ok) sc.vars.emplace_back(kv.first, kv.second.v);
		return evalIn(r, sc, snap.vals);
	}

	bool Engine::pageOn(int page) const
	{
		if (!def || page < 0 || page >= NUM_PAGES) return false;
		const NumRef& w = def->pages[page].when;
		if (!w.set) return true;
		Val v = eval(w);
		return v.ok && v.v.truthy();
	}

	void Engine::finish()
	{
		if (!def) return;
		std::vector<bool> need;
		neededVars(need);
		auto rom = romBytes();	// (kept for this pass)
		Snapshot s;
		s.vals = snap.vals;	// (values not read in this set: as they were)
		s.lists = snap.lists;
		bool any = false, again = false;
		for (auto& c : chunks) any = any || !c.bytes.empty();
		s.seen = any || chunks.empty() || snap.seen;
		s.error = any || chunks.empty() ? "" : err;
		err.clear();
		// memory and constants first, then expressions (in the order they resolve; a loop leaves the rest unread)
		std::vector<int> state(def->vars.size(), 0);	// 0 to do, 1 doing, 2 done
		Scope sc;
		std::function<void(int)> ev = [&](int i)
		{
			if (state[i]) return;
			state[i] = 1;
			const Var& v = def->vars[i];
			Val out;
			if (v.kind == Var::CONST) { out.ok = true; out.v = v.value; }
			else if (v.kind == Var::MEM)
			{
				if (!v.m.ptr)
				{
					const uint8_t* p = at(v.m.addr, v.m.size);
					if (p) { out.ok = true; out.v = decode(v.m, p, *def); }
					else if (inRam(v.m.addr, v.m.size) && chunks.empty()) out = s.vals[v.name];
				}
				else
				{
					const uint8_t* pp = at(v.m.ptrAddr, 4);
					if (pp)
					{
						uint32_t ptr = le(pp, 4), t = ptr + (uint32_t)v.m.ptrOff;
						bool had = ptrSeen.count(v.name);
						ptrSeen[v.name] = ptr;
						auto used = ptrUsed.find(v.name);
						if (ptr == 0 || (!inRam(t, v.m.size) && !inRom(t, v.m.size))) {}	// not available
						else if (inRom(t, v.m.size))
						{
							startRom();
							const uint8_t* p = at(t, v.m.size);
							if (p) { out.ok = true; out.v = decode(v.m, p, *def); }
						}
						else if (used != ptrUsed.end() && used->second == t)
						{
							const uint8_t* p = at(t, v.m.size);
							if (p) { out.ok = true; out.v = decode(v.m, p, *def); }
						}
						else { out = s.vals[v.name]; again = true; (void)had; }	// (moved: the old value, asked again at once)
					}
					else out = s.vals[v.name];
				}
			}
			else
			{
				bool ok = true;
				for (auto& n : v.deps)
				{
					auto it = def->varIndex.find(n);
					if (it == def->varIndex.end()) { ok = false; break; }
					ev(it->second);
					auto x = s.vals.find(n);
					if (x == s.vals.end() || !x->second.ok) { ok = false; break; }
				}
				if (ok && state[i] == 1)
				{
					Scope e;
					for (auto& n : v.deps) e.vars.emplace_back(n, s.vals[n].v);
					out.ok = true;
					out.v = evalExpr(v.expr, e);
				}
			}
			s.vals[v.name] = out;
			state[i] = 2;
		};
		for (size_t i = 0; i < def->vars.size(); i++) if (need[i]) ev((int)i);
		for (auto& kv : s.vals) if (kv.second.ok) sc.vars.emplace_back(kv.first, kv.second.v);
		// the lists on the page shown
		if (wantPage >= 0 && wantPage < NUM_PAGES)
			for (auto& w : def->pages[wantPage].widgets)
			{
				if (w.type != Widget::LIST) continue;
				auto& rows = s.lists[w.listId];
				uint32_t b = w.base;
				bool ok = true;
				if (w.basePtr)
				{
					std::string key = "#list" + std::to_string(w.listId);
					const uint8_t* pp = at(w.ptrAddr, 4);
					if (!pp) continue;	// (kept)
					uint32_t ptr = le(pp, 4);
					ptrSeen[key] = ptr;
					b = ptr + (uint32_t)w.ptrOff;
					auto used = ptrUsed.find(key);
					if (ptr == 0) ok = false;
					else if (!inRom(b, 1) && (used == ptrUsed.end() || used->second != b)) { again = true; continue; }
				}
				Val nv = evalIn(w.n, sc, s.vals);
				int n = nv.ok ? std::max(0, std::min(64, (int)nv.v.num())) : 0;
				rows.clear();
				if (!ok) continue;
				for (int i = 0; i < n; i++)
				{
					uint32_t rb = b + (uint32_t)(i * w.stride);
					std::vector<Val> row;
					Scope rsc;
					rsc.parent = &sc;
					bool any = false;
					for (auto& f : w.fields)
					{
						Val fv;
						if (f.m.type == INDEX)
						{
							// the row's number, through the field's post-processing (add 1: counted from one)
							uint8_t ix[4] = { (uint8_t)i, (uint8_t)(i >> 8), 0, 0 };
							Mem m = f.m; m.type = U16; m.size = 2;
							fv.ok = true; fv.v = decode(m, ix, *def);
							rsc.vars.emplace_back(f.name, fv.v);
							row.push_back(fv);
							continue;
						}
						const uint8_t* p = at(rb + f.off, f.m.size);
						if (p) { fv.ok = true; fv.v = decode(f.m, p, *def); any = true; rsc.vars.emplace_back(f.name, fv.v); }
						row.push_back(fv);
					}
					if (!any) break;	// (past what was read)
					if (w.rowWhen.set)
					{
						Val keep = evalIn(w.rowWhen, rsc, s.vals);
						if (!keep.ok || !keep.v.truthy()) continue;
					}
					rows.push_back(row);
				}
			}
		if (again) nextAt = 0;
		bool same = s.seen == snap.seen && s.error == snap.error && s.vals.size() == snap.vals.size() && s.lists.size() == snap.lists.size();
		if (same) for (auto& kv : s.vals) { auto o = snap.vals.find(kv.first); if (o == snap.vals.end() || !(o->second == kv.second)) { same = false; break; } }
		if (same) for (size_t i = 0; i < s.lists.size() && same; i++)
		{
			if (s.lists[i].size() != snap.lists[i].size()) { same = false; break; }
			for (size_t r = 0; r < s.lists[i].size() && same; r++) same = s.lists[i][r] == snap.lists[i][r];
		}
		snap = std::move(s);
		if (!same) changed = true;
	}
}
