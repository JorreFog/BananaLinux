#include "expr.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>

namespace banana
{
	double EV::num() const
	{
		if (!isStr) return n;
		char* e;
		double d = strtod(s.c_str(), &e);
		return e != s.c_str() ? d : 0;
	}
	std::string EV::str() const
	{
		if (isStr) return s;
		if (std::isnan(n)) return "";
		char b[32];
		if (n == std::floor(n) && std::fabs(n) < 1e15) snprintf(b, sizeof b, "%lld", (long long)n);
		else snprintf(b, sizeof b, "%g", n);
		return b;
	}

	bool Scope::lookup(const std::string& name, EV& out) const
	{
		for (const Scope* s = this; s; s = s->parent)
		{
			for (auto& v : s->vars) if (v.first == name) { out = v.second; return true; }
			if (s->resolve && s->resolve(name, out)) return true;
		}
		return false;
	}

	// ---- colours ----------------------------------------------------------------------------------------------------
	bool parseColorText(const std::string& v0, uint32_t& out)
	{
		std::string v = v0;
		while (!v.empty() && isspace((unsigned char)v.back())) v.pop_back();
		while (!v.empty() && isspace((unsigned char)v[0])) v.erase(0, 1);
		if (v.empty()) return false;
		if (v == "transparent" || v == "none") { out = 0; return true; }
		static const std::map<std::string, uint32_t> names = { { "black", 0xff000000 }, { "white", 0xffffffff }, { "red", 0xffff0000 },
			{ "green", 0xff00c000 }, { "blue", 0xff0000ff }, { "yellow", 0xffffff00 }, { "gray", 0xff808080 }, { "grey", 0xff808080 } };
		auto nm = names.find(v);
		if (nm != names.end()) { out = nm->second; return true; }
		if (v[0] == '#')
		{
			std::string h = v.substr(1);
			if (!std::all_of(h.begin(), h.end(), [](char c) { return isxdigit((unsigned char)c); })) return false;
			if (h.size() == 3 || h.size() == 4) { std::string e; for (char c : h) { e += c; e += c; } h = e; }
			if (h.size() == 6) h += "ff";
			if (h.size() != 8) return false;
			uint32_t rgba = (uint32_t)strtoul(h.c_str(), nullptr, 16);
			out = (rgba >> 8) | (rgba & 0xff) << 24;
			return true;
		}
		if (v.compare(0, 4, "rgb(") == 0 || v.compare(0, 5, "rgba(") == 0)
		{
			float c[4] = { 0, 0, 0, 1 };
			int n = sscanf(v.c_str() + v.find('(') + 1, "%f , %f , %f , %f", &c[0], &c[1], &c[2], &c[3]);
			if (n < 3) return false;
			auto b = [](float x) { return (uint32_t)std::min(255.0f, std::max(0.0f, x + 0.5f)); };
			out = b(c[3] * 255) << 24 | b(c[0]) << 16 | b(c[1]) << 8 | b(c[2]);
			return true;
		}
		return false;
	}
	std::string colorText(uint32_t c)
	{
		char b[16];
		snprintf(b, sizeof b, "#%06x%02x", c & 0xffffff, c >> 24);
		return b;
	}
	static uint32_t colorOf(const EV& v)
	{
		uint32_t c = 0xff000000;
		if (v.isStr) parseColorText(v.s, c);
		else c = 0xff000000 | (uint32_t)v.n;
		return c;
	}
	static uint32_t mixColor(uint32_t a, uint32_t b, double t)
	{
		t = std::min(1.0, std::max(0.0, t));
		auto ch = [&](int sh) { double x = (a >> sh) & 255, y = (b >> sh) & 255; return (uint32_t)std::lround(x + (y - x) * t) << sh; };
		return ch(24) | ch(16) | ch(8) | ch(0);
	}

	// ---- the tree ---------------------------------------------------------------------------------------------------
	struct ExprNode
	{
		enum Op { LIT, VAR, NEG, NOT, ADD, SUB, MUL, DIV, MOD, EQ, NE, LT, LE, GT, GE, AND, OR, TERN, CALL } op = LIT;
		EV lit;
		std::string name;	// VAR, CALL
		std::vector<Expr> a;
	};

	namespace
	{
		struct P
		{
			const std::string& s;
			size_t i = 0;
			std::string err;
			explicit P(const std::string& t) : s(t) {}
			void ws() { while (i < s.size() && isspace((unsigned char)s[i])) i++; }
			bool eat(const char* t) { ws(); size_t n = strlen(t); if (s.compare(i, n, t) == 0) { i += n; return true; } return false; }
			Expr node(ExprNode::Op op, std::vector<Expr> a) { auto n = std::make_shared<ExprNode>(); n->op = op; n->a = std::move(a); return n; }
			Expr fail(const std::string& m) { if (err.empty()) err = m + " at " + std::to_string(i); return nullptr; }

			Expr ternary()
			{
				Expr c = orExpr();
				if (!c) return nullptr;
				if (!eat("?")) return c;
				Expr a = ternary();
				if (!a) return nullptr;
				if (!eat(":")) return fail("':' expected");
				Expr b = ternary();
				if (!b) return nullptr;
				return node(ExprNode::TERN, { c, a, b });
			}
			Expr orExpr()
			{
				Expr l = andExpr();
				while (l && eat("||")) { Expr r = andExpr(); if (!r) return nullptr; l = node(ExprNode::OR, { l, r }); }
				return l;
			}
			Expr andExpr()
			{
				Expr l = cmp();
				while (l && eat("&&")) { Expr r = cmp(); if (!r) return nullptr; l = node(ExprNode::AND, { l, r }); }
				return l;
			}
			Expr cmp()
			{
				Expr l = add();
				for (;;)
				{
					if (!l) return nullptr;
					ExprNode::Op op;
					if (eat("==")) op = ExprNode::EQ;
					else if (eat("!=")) op = ExprNode::NE;
					else if (eat("<=")) op = ExprNode::LE;
					else if (eat(">=")) op = ExprNode::GE;
					else if (eat("<")) op = ExprNode::LT;
					else if (eat(">")) op = ExprNode::GT;
					else return l;
					Expr r = add();
					if (!r) return nullptr;
					l = node(op, { l, r });
				}
			}
			Expr add()
			{
				Expr l = mul();
				for (;;)
				{
					if (!l) return nullptr;
					ExprNode::Op op;
					if (eat("+")) op = ExprNode::ADD;
					else if (eat("-")) op = ExprNode::SUB;
					else return l;
					Expr r = mul();
					if (!r) return nullptr;
					l = node(op, { l, r });
				}
			}
			Expr mul()
			{
				Expr l = unary();
				for (;;)
				{
					if (!l) return nullptr;
					ExprNode::Op op;
					if (eat("*")) op = ExprNode::MUL;
					else if (eat("/")) op = ExprNode::DIV;
					else if (eat("%")) op = ExprNode::MOD;
					else return l;
					Expr r = unary();
					if (!r) return nullptr;
					l = node(op, { l, r });
				}
			}
			Expr unary()
			{
				if (eat("-")) { Expr e = unary(); return e ? node(ExprNode::NEG, { e }) : nullptr; }
				if (eat("!")) { Expr e = unary(); return e ? node(ExprNode::NOT, { e }) : nullptr; }
				if (eat("+")) return unary();
				return primary();
			}
			Expr primary()
			{
				ws();
				if (i >= s.size()) return fail("a value expected");
				char c = s[i];
				if (c == '(')
				{
					i++;
					Expr e = ternary();
					if (!e) return nullptr;
					if (!eat(")")) return fail("')' expected");
					return e;
				}
				if (c == '\'' || c == '"')
				{
					std::string v;
					for (i++; i < s.size() && s[i] != c; i++)
					{
						if (s[i] == '\\' && i + 1 < s.size()) { i++; v += s[i] == 'n' ? '\n' : s[i]; }
						else v += s[i];
					}
					if (i >= s.size()) return fail("a string that doesn't end");
					i++;
					auto n = std::make_shared<ExprNode>(); n->lit = EV(v);
					return n;
				}
				if (c == '#')	// a colour without quotes
				{
					size_t b = i++;
					while (i < s.size() && isxdigit((unsigned char)s[i])) i++;
					auto n = std::make_shared<ExprNode>(); n->lit = EV(s.substr(b, i - b));
					return n;
				}
				if (isdigit((unsigned char)c) || c == '.')
				{
					char* e;
					double d = strtod(s.c_str() + i, &e);
					i = e - s.c_str();
					auto n = std::make_shared<ExprNode>(); n->lit = EV(d);
					return n;
				}
				if (isalpha((unsigned char)c) || c == '_' || c == '$')
				{
					size_t b = i;
					while (i < s.size() && (isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '.' || s[i] == '$')) i++;
					std::string id = s.substr(b, i - b);
					if (id == "true" || id == "false") { auto n = std::make_shared<ExprNode>(); n->lit = EV(id == "true" ? 1.0 : 0.0); return n; }
					if (id == "null") { auto n = std::make_shared<ExprNode>(); n->lit = EV(""); return n; }
					if (eat("("))
					{
						auto n = std::make_shared<ExprNode>();
						n->op = ExprNode::CALL; n->name = id;
						if (!eat(")"))
							for (;;)
							{
								Expr a = ternary();
								if (!a) return nullptr;
								n->a.push_back(a);
								if (eat(")")) break;
								if (!eat(",")) return fail("',' or ')' expected");
							}
						return n;
					}
					auto n = std::make_shared<ExprNode>();
					n->op = ExprNode::VAR; n->name = id[0] == '$' ? id.substr(1) : id;
					return n;
				}
				return fail(std::string("unexpected '") + c + "'");
			}
		};

		const double PI = 3.14159265358979323846;
		double clamp01(double t) { return std::min(1.0, std::max(0.0, t)); }

		EV call(const ExprNode& n, const Scope& sc)
		{
			std::vector<EV> v;
			v.reserve(n.a.size());
			const std::string& f = n.name;
			// if() only works out the side it takes
			if (f == "if") return n.a.size() >= 2 ? (evalExpr(n.a[0], sc).truthy() ? evalExpr(n.a[1], sc) : n.a.size() > 2 ? evalExpr(n.a[2], sc) : EV(0.0)) : EV(0.0);
			for (auto& a : n.a) v.push_back(evalExpr(a, sc));
			auto N = [&](size_t k, double def = 0) { return k < v.size() ? v[k].num() : def; };
			auto S = [&](size_t k) { return k < v.size() ? v[k].str() : std::string(); };
			if (f == "min") { double r = N(0); for (size_t k = 1; k < v.size(); k++) r = std::min(r, N(k)); return r; }
			if (f == "max") { double r = N(0); for (size_t k = 1; k < v.size(); k++) r = std::max(r, N(k)); return r; }
			if (f == "clamp") return std::min(N(2, 1), std::max(N(1), N(0)));
			if (f == "abs") return std::fabs(N(0));
			if (f == "floor") return std::floor(N(0));
			if (f == "ceil") return std::ceil(N(0));
			if (f == "round") { double m = std::pow(10.0, N(1)); return std::round(N(0) * m) / m; }
			if (f == "sqrt") return std::sqrt(std::max(0.0, N(0)));
			if (f == "pow") return std::pow(N(0), N(1));
			if (f == "sign") return (double)((N(0) > 0) - (N(0) < 0));
			if (f == "mod") { double b = N(1, 1); if (b == 0) return 0.0; double r = std::fmod(N(0), b); return r < 0 ? r + b : r; }
			if (f == "sin") return std::sin(N(0));
			if (f == "cos") return std::cos(N(0));
			if (f == "sind") return std::sin(N(0) * PI / 180);
			if (f == "cosd") return std::cos(N(0) * PI / 180);
			if (f == "lerp") return N(0) + (N(1) - N(0)) * N(2);
			if (f == "step") return N(1) >= N(0) ? 1.0 : 0.0;
			if (f == "smoothstep") { double t = clamp01((N(2) - N(0)) / (N(1) - N(0) == 0 ? 1 : N(1) - N(0))); return t * t * (3 - 2 * t); }
			// progress 0..1 of something that started `ago` ms ago and takes `ms`: progress(moved, 200)
			if (f == "progress") return clamp01(N(0) / std::max(1.0, N(1, 1)));
			if (f == "easeIn") { double t = clamp01(N(0)); return t * t * t; }
			if (f == "easeOut") { double t = 1 - clamp01(N(0)); return 1 - t * t * t; }
			if (f == "easeInOut") { double t = clamp01(N(0)); return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2; }
			if (f == "easeOutBack") { double t = clamp01(N(0)) - 1, c = N(1, 1.70158); return 1 + (c + 1) * t * t * t + c * t * t; }
			if (f == "easeOutElastic")
			{
				double t = clamp01(N(0));
				return t == 0 || t == 1 ? t : std::pow(2, -10 * t) * std::sin((t * 10 - 0.75) * (2 * PI / 3)) + 1;
			}
			if (f == "bounce")
			{
				double t = clamp01(N(0)), k = 7.5625, d = 2.75;
				if (t < 1 / d) return k * t * t;
				if (t < 2 / d) { t -= 1.5 / d; return k * t * t + 0.75; }
				if (t < 2.5 / d) { t -= 2.25 / d; return k * t * t + 0.9375; }
				t -= 2.625 / d; return k * t * t + 0.984375;
			}
			// a wave between 0 and 1 with a period of `ms` (from the time hook): pulse(time, 1200)
			if (f == "pulse") return 0.5 - 0.5 * std::cos(N(0) * 2 * PI / std::max(1.0, N(1, 1000)));
			if (f == "mix") return EV(colorText(mixColor(colorOf(v.size() > 0 ? v[0] : EV()), colorOf(v.size() > 1 ? v[1] : EV()), N(2, 0.5))));
			if (f == "alpha") { uint32_t c = colorOf(v.size() > 0 ? v[0] : EV()); return EV(colorText((c & 0xffffff) | (uint32_t)std::lround(clamp01(N(1, 1)) * ((c >> 24) & 255)) << 24)); }
			if (f == "lighten") { uint32_t c = colorOf(v.size() > 0 ? v[0] : EV()); return EV(colorText(mixColor(c, (c & 0xff000000) | 0xffffff, N(1, 0.2)))); }
			if (f == "darken") { uint32_t c = colorOf(v.size() > 0 ? v[0] : EV()); return EV(colorText(mixColor(c, c & 0xff000000, N(1, 0.2)))); }
			if (f == "rgb" || f == "rgba")
			{
				auto b = [](double x) { return (uint32_t)std::min(255.0, std::max(0.0, x + 0.5)); };
				return EV(colorText(b(N(3, 1) * 255) << 24 | b(N(0)) << 16 | b(N(1)) << 8 | b(N(2))));
			}
			if (f == "hsl")
			{
				double h = std::fmod(N(0), 360) / 60, s = clamp01(N(1)), l = clamp01(N(2)), c = (1 - std::fabs(2 * l - 1)) * s;
				if (h < 0) h += 6;
				double x = c * (1 - std::fabs(std::fmod(h, 2) - 1)), m = l - c / 2, r = 0, g = 0, bb = 0;
				int k = (int)h;
				if (k == 0) { r = c; g = x; } else if (k == 1) { r = x; g = c; } else if (k == 2) { g = c; bb = x; }
				else if (k == 3) { g = x; bb = c; } else if (k == 4) { r = x; bb = c; } else { r = c; bb = x; }
				auto b = [](double y) { return (uint32_t)std::min(255.0, std::max(0.0, y * 255 + 0.5)); };
				return EV(colorText(b(N(3, 1)) << 24 | b(r + m) << 16 | b(g + m) << 8 | b(bb + m)));
			}
			if (f == "upper") { std::string s = S(0); for (auto& c : s) c = (char)toupper((unsigned char)c); return EV(s); }
			if (f == "lower") { std::string s = S(0); for (auto& c : s) c = (char)tolower((unsigned char)c); return EV(s); }
			if (f == "len")
			{
				std::string s = S(0);
				return (double)std::count_if(s.begin(), s.end(), [](char c) { return ((unsigned char)c & 0xc0) != 0x80; });
			}
			if (f == "substr")
			{
				// by characters (UTF-8), not bytes
				std::string s = S(0), r;
				int from = (int)N(1), n = v.size() > 2 ? (int)N(2) : 1 << 30, k = -1;
				for (size_t b = 0; b < s.size(); b++)
				{
					if (((unsigned char)s[b] & 0xc0) != 0x80) k++;
					if (k >= from && k < from + n) r += s[b];
				}
				return EV(r);
			}
			if (f == "contains") return S(0).find(S(1)) != std::string::npos ? 1.0 : 0.0;
			if (f == "replace")
			{
				std::string s = S(0), a = S(1), b = S(2);
				if (!a.empty()) for (size_t at = 0; (at = s.find(a, at)) != std::string::npos; at += b.size()) s.replace(at, a.size(), b);
				return EV(s);
			}
			if (f == "pad") { std::string s = S(0); while ((int)s.size() < (int)N(1)) s = (v.size() > 2 ? S(2) : std::string("0")) + s; return EV(s); }
			if (f == "fixed") { char b[64]; snprintf(b, sizeof b, "%.*f", std::max(0, std::min(10, (int)N(1))), N(0)); return EV(b); }
			if (f == "str") return EV(S(0));
			if (f == "num") return N(0);
			// plural(3, 'game') "3 games", plural(1, 'game') "1 game", plural(n, 'match', 'matches')
			if (f == "plural") return EV(EV(N(0)).str() + " " + (N(0) == 1 ? S(1) : (v.size() > 2 ? S(2) : S(1) + "s")));
			// pick(i, a, b, c): the i-th of the rest (clamped)
			if (f == "pick") { if (v.size() < 2) return EV(""); int k = std::max(0, std::min((int)v.size() - 2, (int)N(0))); return v[k + 1]; }
			// the first of its arguments that isn't empty
			if (f == "either") { for (auto& e : v) if (e.truthy()) return e; return v.empty() ? EV("") : v.back(); }
			return EV(0.0);
		}
	}

	Expr compileExpr(const std::string& text, std::string* why)
	{
		P p(text);
		Expr e = p.ternary();
		if (e) { p.ws(); if (p.i < text.size()) e = p.fail("more after the end"); }
		if (!e && why) *why = p.err + " in \"" + text + "\"";
		return e;
	}

	EV evalExpr(const Expr& e, const Scope& sc)
	{
		if (!e) return EV(0.0);
		const ExprNode& n = *e;
		switch (n.op)
		{
		case ExprNode::LIT: return n.lit;
		case ExprNode::VAR: { EV v; if (!sc.lookup(n.name, v)) v = EV(""); return v; }
		case ExprNode::NEG: return -evalExpr(n.a[0], sc).num();
		case ExprNode::NOT: return evalExpr(n.a[0], sc).truthy() ? 0.0 : 1.0;
		case ExprNode::AND: return evalExpr(n.a[0], sc).truthy() && evalExpr(n.a[1], sc).truthy() ? 1.0 : 0.0;
		case ExprNode::OR: { EV a = evalExpr(n.a[0], sc); return a.truthy() ? a : evalExpr(n.a[1], sc); }
		case ExprNode::TERN: return evalExpr(n.a[0], sc).truthy() ? evalExpr(n.a[1], sc) : evalExpr(n.a[2], sc);
		case ExprNode::CALL: return call(n, sc);
		default: break;
		}
		EV a = evalExpr(n.a[0], sc), b = evalExpr(n.a[1], sc);
		switch (n.op)
		{
		case ExprNode::ADD: if (a.isStr || b.isStr) return EV(a.str() + b.str()); return a.n + b.n;
		case ExprNode::SUB: return a.num() - b.num();
		case ExprNode::MUL: return a.num() * b.num();
		case ExprNode::DIV: return b.num() == 0 ? 0.0 : a.num() / b.num();
		case ExprNode::MOD: { double d = b.num(); return d == 0 ? 0.0 : std::fmod(a.num(), d); }
		case ExprNode::EQ: return (a.isStr || b.isStr ? a.str() == b.str() : a.n == b.n) ? 1.0 : 0.0;
		case ExprNode::NE: return (a.isStr || b.isStr ? a.str() != b.str() : a.n != b.n) ? 1.0 : 0.0;
		case ExprNode::LT: return a.num() < b.num() ? 1.0 : 0.0;
		case ExprNode::LE: return a.num() <= b.num() ? 1.0 : 0.0;
		case ExprNode::GT: return a.num() > b.num() ? 1.0 : 0.0;
		case ExprNode::GE: return a.num() >= b.num() ? 1.0 : 0.0;
		default: return 0.0;
		}
	}

	Prop Prop::parse(const std::string& t)
	{
		Prop p;
		if (!t.empty() && t[0] == '=')
		{
			p.kind = EXPR;
			p.expr = compileExpr(t.substr(1), &p.error);
			return p;
		}
		if (t.find('{') == std::string::npos) { p.lit = EV(t); return p; }
		p.kind = TEMPLATE;
		std::string text;
		for (size_t i = 0; i < t.size(); i++)
		{
			if (t[i] == '{' && i + 1 < t.size() && t[i + 1] == '{') { text += '{'; i++; continue; }
			if (t[i] == '}' && i + 1 < t.size() && t[i + 1] == '}') { text += '}'; i++; continue; }
			if (t[i] != '{') { text += t[i]; continue; }
			size_t e = t.find('}', i);
			if (e == std::string::npos) { text += t.substr(i); break; }
			std::string why;
			Expr x = compileExpr(t.substr(i + 1, e - i - 1), &why);
			if (!x && p.error.empty()) p.error = why;
			p.parts.emplace_back(text, x);
			text.clear();
			i = e;
		}
		p.parts.emplace_back(text, nullptr);
		return p;
	}

	EV Prop::eval(const Scope& sc) const
	{
		if (kind == LITERAL) return lit;
		if (kind == EXPR) return evalExpr(expr, sc);
		std::string r;
		for (auto& pt : parts) { r += pt.first; if (pt.second) r += evalExpr(pt.second, sc).str(); }
		return EV(r);
	}
}
