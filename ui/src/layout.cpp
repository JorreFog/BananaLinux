#include "layout.h"
#include "library.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <time.h>

namespace banana
{
	namespace
	{
		double monoMs()
		{
			timespec ts;
			clock_gettime(CLOCK_MONOTONIC, &ts);
			return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
		}
		std::string hex(const rnds::Color& c)
		{
			char b[16];
			snprintf(b, sizeof b, "#%02x%02x%02x", (int)std::lround(c.r * 255), (int)std::lround(c.g * 255), (int)std::lround(c.b * 255));
			return b;
		}
		std::string hex(uint32_t rgb) { char b[16]; snprintf(b, sizeof b, "#%06x", rgb & 0xffffff); return b; }
		rnds::Color colorFrom(uint32_t argb, float opacity)
		{
			return rnds::Color(((argb >> 16) & 255) / 255.0f, ((argb >> 8) & 255) / 255.0f, (argb & 255) / 255.0f, ((argb >> 24) & 255) / 255.0f * opacity);
		}
	}

	bool isLayoutTheme(const std::string& path)
	{
		if (path.empty()) return false;
		JPtr j = readJsonFile(path);
		return j && j->get("screens") && j->get("screens")->isObj();
	}

	// ---- the compiled theme ------------------------------------------------------------------------------------------
	struct LayoutMenu::Node
	{
		std::string type;					// rect, text, image, group, list
		std::map<std::string, Prop> props;	// flattened: "shadow.blur", "gradient.stops.0", "radius.2"
		std::vector<std::shared_ptr<Node>> children;
		std::shared_ptr<Node> item;			// a list's template
		struct State { Prop when; std::map<std::string, Prop> props; };
		std::vector<State> states;			// overrides while their condition holds (the last one wins)
		std::vector<std::pair<std::string, Prop>> with;	// a component's parameters, set in its children's scope
		std::map<std::string, double> smooth;	// property -> time constant (ms): the value eases towards its target
		int serial = 0;
		const Prop* prop(const std::string& k) const { auto it = props.find(k); return it == props.end() ? nullptr : &it->second; }
	};
	typedef std::shared_ptr<LayoutMenu::Node> NodePtr;

	struct LayoutMenu::Impl
	{
		LayoutMenu* m;
		float s;						// device px per layout px (a screen is 480 high)
		float W = 640, H = 480;			// the screens in layout px
		std::string path, dir;
		JPtr doc;
		std::map<std::string, JPtr> components;
		std::vector<std::pair<std::string, Prop>> vars;
		std::map<std::string, std::vector<NodePtr>> screens;	// "home.top", "library.bottom", "all.top", ...
		std::map<std::string, std::map<std::string, std::string>> input;	// view -> button -> action
		std::map<std::string, std::vector<std::pair<std::string, std::string>>> hintSets;
		bool extraHints = true;
		double launchMs = 900, settleMs = 600, scrollMs = 70, fps = 30;
		std::vector<std::pair<std::string, double>> cues;
		bool wrap = false, debug = false;
		int serials = 0;

		// fonts by name: the theme's, then the built-in ones
		std::map<std::string, std::string> fontFiles;
		std::map<std::string, std::shared_ptr<rnds::Font>> fonts;
		std::string defaultFont = "pixel";

		// data
		std::vector<rnds::SysInfo> systems;
		std::vector<rnds::GameInfo> games;
		std::string clock, date;
		double now = 0, last = 0, start = -1;
		double movedAt = -1e9, enteredAt = -1e9;
		int moveDir = 1;
		float scroll = 0;				// the cursor's eased position
		double launchAt = -1;
		int launchGame = -1;
		size_t cuesDone = 0;
		mutable bool usedTime = false;	// the last frame read "time": it moves by itself
		double lastFrame = 0;

		// per element state: eased values and lists' first visible rows
		struct Smoothed { double v = 0, target = 0; bool set = false; double at = 0; };
		std::map<std::string, Smoothed> smoothed;
		bool smoothMoving = false;

		// what a tap can hit (device px), from the last frame
		struct Hit { int screen; float x0, y0, x1, y1; std::string source; int index; std::string action; };
		std::vector<Hit> hits;

		// textures
		struct TextTex { rnds::TexPtr tex; int padX = 0, padY = 0; float w = 0; };
		std::map<std::string, TextTex> texts;
		std::map<std::string, rnds::TexPtr> shapes;
		struct ImgTex { rnds::TexPtr tex; float ox = 0, oy = 0, w = 0, h = 0; };	// placed in the box (device px)
		std::map<std::string, ImgTex> images;	// by key (src, fit, size ...)
		std::map<std::string, std::string> latest;	// src -> its last key made (drawn scaled while another size is made)
		std::deque<std::string> imageOrder;
		struct Job { std::string key, src, fit, crop; int w, h; float radius; };
		std::deque<Job> jobs;
		std::set<std::string> pending, pendingSrc, failed;
		std::vector<std::pair<std::string, ImgTex>> done;

		void err(const std::string& e)
		{
			if (std::find(m->mErrors.begin(), m->mErrors.end(), e) != m->mErrors.end()) return;
			m->mErrors.push_back(e);
			fprintf(stderr, "banana-ui: theme: %s\n", e.c_str());
		}

		// ---- loading ---------------------------------------------------------------------------------------------
		void flatten(const std::string& prefix, const JVal& v, std::map<std::string, Prop>& out, const std::string& where)
		{
			switch (v.kind)
			{
			case JVal::OBJ: for (auto& e : v.obj) flatten(prefix + "." + e.first, *e.second, out, where); break;
			case JVal::ARR: for (size_t k = 0; k < v.arr.size(); k++) flatten(prefix + "." + std::to_string(k), *v.arr[k], out, where); break;
			case JVal::STR:
			{
				Prop p = Prop::parse(v.s);
				if (!p.error.empty()) err(where + "." + prefix + ": " + p.error);
				out[prefix] = p;
				break;
			}
			case JVal::NUM: out[prefix] = Prop::number(v.n); break;
			case JVal::BOOL: out[prefix] = Prop::number(v.b ? 1 : 0); break;
			default: break;
			}
		}

		// a component's root with the instance's members over it
		JPtr expand(const JVal& v, int depth, const std::string& where)
		{
			const JVal* use = v.get("use");
			if (!use || !use->isStr()) return nullptr;
			auto it = components.find(use->s);
			if (it == components.end()) { err(where + ": no component \"" + use->s + "\""); return nullptr; }
			if (depth > 16) { err(where + ": components used inside themselves"); return nullptr; }
			auto merged = std::make_shared<JVal>(*it->second);
			for (auto& e : v.obj)
			{
				if (e.first == "use") continue;
				bool found = false;
				for (auto& me : merged->obj) if (me.first == e.first) { me.second = e.second; found = true; }
				if (!found) merged->obj.push_back(e);
			}
			// the component may itself be another one's instance
			const JVal* inner = it->second->get("use");
			if (inner && inner->isStr())
			{
				auto again = std::make_shared<JVal>(*merged);
				for (auto& me : again->obj) if (me.first == "use") me.second = std::make_shared<JVal>(*inner);
				JPtr r = expand(*again, depth + 1, where);
				if (r) return r;
			}
			auto& o = merged->obj;
			o.erase(std::remove_if(o.begin(), o.end(), [](const std::pair<std::string, JPtr>& e) { return e.first == "use"; }), o.end());
			return merged;
		}

		NodePtr compile(const JVal& v0, const std::string& where, int depth = 0)
		{
			if (depth > 48) { err(where + ": nested too deep"); return nullptr; }
			JPtr hold;
			const JVal* vp = &v0;
			if (v0.isObj() && v0.get("use")) { hold = expand(v0, depth, where); if (!hold) return nullptr; vp = hold.get(); }
			const JVal& v = *vp;
			if (v.isArr())	// a list of elements: a group of them
			{
				auto g = std::make_shared<Node>();
				g->type = "group";
				g->serial = ++serials;
				for (size_t k = 0; k < v.arr.size(); k++)
					if (NodePtr c = compile(*v.arr[k], where + "[" + std::to_string(k) + "]", depth + 1)) g->children.push_back(c);
				return g;
			}
			if (!v.isObj()) { err(where + ": an element is an object"); return nullptr; }
			auto n = std::make_shared<Node>();
			n->serial = ++serials;
			n->type = v.str("type", v.get("children") ? "group" : v.get("text") ? "text" : v.get("src") ? "image" : v.get("source") ? "list" : "rect");
			static const std::set<std::string> types = { "rect", "text", "image", "group", "list" };
			if (!types.count(n->type)) { err(where + ": unknown type \"" + n->type + "\""); return nullptr; }
			std::string here = where + (v.get("id") ? "#" + v.str("id") : "") + "(" + n->type + ")";
			for (auto& e : v.obj)
			{
				const std::string& k = e.first;
				if (k == "type" || k == "id" || k == "comment" || k == "//") continue;
				if (k == "children")
				{
					const JVal& c = *e.second;
					if (c.isArr()) { for (size_t i = 0; i < c.arr.size(); i++) if (NodePtr ch = compile(*c.arr[i], here + "[" + std::to_string(i) + "]", depth + 1)) n->children.push_back(ch); }
					else if (NodePtr ch = compile(c, here + ".children", depth + 1)) n->children.push_back(ch);
					continue;
				}
				if (k == "item") { n->item = compile(*e.second, here + ".item", depth + 1); continue; }
				if (k == "states")
				{
					if (!e.second->isArr()) { err(here + ".states: a list of { \"when\": ..., properties }"); continue; }
					for (auto& st : e.second->arr)
					{
						if (!st->isObj()) continue;
						Node::State S;
						S.when = Prop::parse(st->str("when", "0"));
						for (auto& se : st->obj) if (se.first != "when") flatten(se.first, *se.second, S.props, here + ".states");
						n->states.push_back(S);
					}
					continue;
				}
				if (k == "with" || k == "props")
				{
					if (!e.second->isObj()) continue;
					for (auto& pe : e.second->obj)
					{
						std::map<std::string, Prop> one;
						flatten(pe.first, *pe.second, one, here + ".with");
						for (auto& o : one) n->with.emplace_back(o.first, o.second);
					}
					continue;
				}
				if (k == "smooth")
				{
					if (e.second->isObj()) for (auto& se : e.second->obj) n->smooth[se.first] = se.second->isNum() ? se.second->n : 150;
					else if (e.second->isNum()) for (const char* p : { "x", "y", "w", "h", "opacity", "scale" }) n->smooth[p] = e.second->n;
					continue;
				}
				flatten(k, *e.second, n->props, here);
			}
			return n;
		}

		std::vector<NodePtr> compileScreen(const JVal* v, const std::string& where)
		{
			std::vector<NodePtr> r;
			if (!v) return r;
			if (v->isArr()) { for (size_t k = 0; k < v->arr.size(); k++) if (NodePtr n = compile(*v->arr[k], where + "[" + std::to_string(k) + "]")) r.push_back(n); }
			else if (NodePtr n = compile(*v, where)) r.push_back(n);
			return r;
		}

		std::string file(const std::string& name) const
		{
			if (name.empty() || name[0] == '/') return name;
			return dir + "/" + name;
		}

		void load(const std::string& pixelFonts, const std::string& interFonts)
		{
			fontFiles["pixel"] = pixelFonts + "/PixelifySans-Regular.ttf";
			fontFiles["pixel-medium"] = pixelFonts + "/PixelifySans-Medium.ttf";
			fontFiles["inter"] = interFonts + "/Inter-Regular.ttf";
			fontFiles["inter-medium"] = interFonts + "/Inter-Medium.ttf";
			fontFiles["inter-semibold"] = interFonts + "/Inter-SemiBold.ttf";
			fontFiles["inter-bold"] = interFonts + "/Inter-Bold.ttf";
			std::string why;
			doc = readJsonFile(path, &why);
			if (!doc) { err(why); return; }
			if (const JVal* f = doc->get("fonts"))
				for (auto& e : f->obj) if (e.second->isStr()) fontFiles[e.first] = file(e.second->s);
			if (fontFiles.count("regular")) defaultFont = "regular";
			defaultFont = doc->str("font", defaultFont);
			if (const JVal* c = doc->get("components")) for (auto& e : c->obj) components[e.first] = e.second;
			if (const JVal* vv = doc->get("vars"))
				for (auto& e : vv->obj)
				{
					std::map<std::string, Prop> one;
					flatten(e.first, *e.second, one, "vars");
					for (auto& o : one) vars.emplace_back(o.first, o.second);
				}
			// the accent and the palette are the Settings screen's (theme.cpp): their colours are hooks here too
			if (const JVal* b = doc->get("behavior"))
			{
				launchMs = b->num("launchMs", launchMs);
				settleMs = b->num("settleMs", settleMs);
				scrollMs = b->num("scrollMs", scrollMs);
				fps = std::max(1.0, std::min(60.0, b->num("fps", fps)));
				wrap = b->flag("wrap", wrap);
				extraHints = b->flag("extraHints", extraHints);
				if (const JVal* c = b->get("cues")) for (auto& e : c->obj) if (e.second->isNum()) cues.emplace_back(e.first, e.second->n);
				std::sort(cues.begin(), cues.end(), [](const std::pair<std::string, double>& a, const std::pair<std::string, double>& b) { return a.second < b.second; });
			}
			else cues.emplace_back("insert", 560);
			debug = doc->flag("debug");
			const JVal* sc = doc->get("screens");
			for (auto& e : sc->obj)
			{
				if (!e.second->isObj()) { err("screens." + e.first + ": { \"top\": ..., \"bottom\": ... }"); continue; }
				for (const char* side : { "top", "bottom" })
					screens[e.first + "." + side] = compileScreen(e.second->get(side), "screens." + e.first + "." + side);
			}
			// the buttons: Carts' by default
			input["home"] = { { "left", "prev" }, { "up", "prev" }, { "l", "prev" }, { "right", "next" }, { "down", "next" }, { "r", "next" },
				{ "a", "open" }, { "x", "resume" }, { "y", "fav" } };
			input["library"] = { { "left", "prev" }, { "right", "next" }, { "up", "prev:10" }, { "down", "next:10" }, { "l", "prevSystem" },
				{ "r", "nextSystem" }, { "a", "open" }, { "b", "back" }, { "y", "fav" } };
			if (const JVal* in = doc->get("input"))
				for (auto& v : in->obj)
					if (v.second->isObj()) for (auto& b : v.second->obj) input[v.first][b.first] = b.second->isStr() ? b.second->s : "none";
			hintSets["home"] = { { "A", "Open" }, { "Y", "Favorite" } };
			hintSets["library"] = { { "A", "Play" }, { "B", "Back" }, { "Y", "Favorite" } };
			if (const JVal* h = doc->get("hints"))
				for (auto& v : h->obj)
				{
					if (!v.second->isArr()) continue;
					auto& set = hintSets[v.first];
					set.clear();
					for (auto& p : v.second->arr)
					{
						if (p->isArr() && p->arr.size() >= 2) set.emplace_back(p->arr[0]->s, p->arr[1]->s);
						else if (p->isObj()) set.emplace_back(p->str("key"), p->str("label"));
					}
				}
		}

		// ---- the data --------------------------------------------------------------------------------------------
		void rebuild()
		{
			systems.clear();
			games.clear();
			int n = m->src->systemCount();
			for (int i = 0; i < n; i++) systems.push_back(m->src->system(i));
			m->mSys = std::min(std::max(0, m->mSys), std::max(0, n - 1));
			if (m->mView != rnds::UI::HOME)
			{
				int g = m->src->gameCount();
				for (int i = 0; i < g; i++) games.push_back(m->src->game(i));
				m->mGame = std::min(std::max(0, m->mGame), std::max(0, g - 1));
			}
		}
		int count() const { return m->mView == rnds::UI::HOME ? (int)systems.size() : (int)games.size(); }
		int cursor() const { return m->mView == rnds::UI::HOME ? m->mSys : m->mGame; }
		std::string viewName() const { return m->mView == rnds::UI::HOME ? "home" : m->mView == rnds::UI::LIBRARY ? "library" : "launch"; }

		bool sysField(const rnds::SysInfo& si, int index, const std::string& f, EV& out) const
		{
			if (f == "id") out = EV(si.id);
			else if (f == "name") out = EV(si.name);
			else if (f == "maker") out = EV(si.maker);
			else if (f == "color" || f == "accent") out = EV(hex(si.accent));
			else if (f == "icon") out = EV(si.icon);
			else if (f == "games") out = EV((double)si.games);
			else if (f == "played") out = EV((double)si.played);
			else if (f == "time") out = EV(si.time);
			else if (f == "fav") out = EV(si.fav ? 1.0 : 0.0);
			else if (f == "app") out = EV(si.app ? 1.0 : 0.0);
			else if (f == "hasResume") out = EV(si.hasResume ? 1.0 : 0.0);
			else if (f == "resumeTitle") out = EV(si.resumeTitle);
			else if (f == "resumeEyebrow") out = EV(si.resumeEyebrow);
			else if (f == "resumeBox") out = EV(si.resumeBox);
			else if (f == "resumeCart") out = EV(si.resumeCart);
			else if (f == "resumeLabel") out = EV(si.resumeLabel);
			else if (f == "resumeArt") out = EV(!si.resumeBox.empty() ? si.resumeBox : !si.resumeLabel.empty() ? si.resumeLabel : si.resumeCart);
			else if (f == "index") out = EV((double)index);
			else return false;
			return true;
		}
		bool gameField(const rnds::GameInfo& g, int index, const std::string& f, EV& out) const
		{
			if (f == "title") out = EV(g.title);
			else if (f == "sub") out = EV(g.sub);
			else if (f == "plays") out = EV(g.plays);
			else if (f == "last") out = EV(g.last);
			else if (f == "time") out = EV(g.time);
			else if (f == "box") out = EV(g.box);
			else if (f == "cart") out = EV(g.cart);
			else if (f == "label") out = EV(g.label);
			else if (f == "art") out = EV(!g.box.empty() ? g.box : !g.label.empty() ? g.label : g.cart);
			else if (f == "rom") out = EV(g.rom);
			else if (f == "romIcon") out = EV(g.rom.empty() ? std::string() : "romicon:" + g.rom);
			else if (f == "icon") out = EV(g.icon);
			else if (f == "achA" || f == "achievements") out = EV((double)g.achA);
			else if (f == "achB" || f == "achievementsTotal") out = EV((double)g.achB);
			else if (f == "achLabel") out = EV(g.achLabel);
			else if (f == "achProgress") out = EV(g.achB > 0 ? (double)g.achA / g.achB : 0.0);
			else if (f == "fav") out = EV(g.fav ? 1.0 : 0.0);
			else if (f == "index") out = EV((double)index);
			else if (f == "system" || f == "systemName" || f == "systemId")
			{
				Library* lib = dynamic_cast<Library*>(m->src);
				if (lib && index >= 0 && index < m->src->gameCount()) out = EV(f == "systemName" ? lib->systemOf(index).name : lib->systemOf(index).id);
				else out = EV(m->mSys < (int)systems.size() ? (f == "systemName" ? systems[m->mSys].name : systems[m->mSys].id) : std::string());
			}
			else return false;
			return true;
		}

		// the menu's hooks: everything a theme's expressions can read
		bool hook(const std::string& n, EV& out) const
		{
			auto pre = [&](const char* p, std::string& rest) { size_t l = strlen(p); if (n.compare(0, l, p) != 0) return false; rest = n.substr(l); return true; };
			std::string rest;
			if (n == "time") { usedTime = true; out = EV(now - start); return true; }
			if (n == "view") { out = EV(viewName()); return true; }
			if (n == "W") { out = EV((double)W); return true; }
			if (n == "H") { out = EV((double)H); return true; }
			if (n == "clock") { out = EV(clock); return true; }
			if (n == "date") { out = EV(date); return true; }
			if (n == "battery")
			{
				float b = m->src->battery();
				out = EV(b < 0 ? -1.0 : std::round(b * 100));
				return true;
			}
			if (n == "charging") { Library* lib = dynamic_cast<Library*>(m->src); out = EV(lib && lib->power().charging ? 1.0 : 0.0); return true; }
			if (n == "batteryMode") { out = EV(m->src->batteryMode()); return true; }
			if (n == "online") { out = EV(m->src->online() ? 1.0 : 0.0); return true; }
			if (n == "motion") { out = EV(m->src->topMotion() ? 1.0 : 0.0); return true; }
			if (n == "brand") { out = EV(rnds::brandNote); return true; }
			if (n == "accent" || n == "accent.base") { out = EV(hex(rnds::accent.base)); return true; }
			if (n == "accent.light") { out = EV(hex(rnds::accent.light)); return true; }
			if (n == "accent.dark") { out = EV(hex(rnds::accent.dark)); return true; }
			if (n == "accent.band") { out = EV(hex(rnds::accent.band)); return true; }
			if (n == "accent.low") { out = EV(hex(rnds::accent.low)); return true; }
			if (pre("palette.", rest)) { rnds::Color c; if (rnds::themeColor(rest.c_str(), c)) { out = EV(hex(c)); return true; } out = EV(""); return true; }
			if (n == "cursor") { out = EV((double)cursor()); return true; }
			if (n == "scroll") { out = EV((double)scroll); return true; }
			if (n == "count") { out = EV((double)count()); return true; }
			if (n == "moved") { out = EV(now - movedAt); return true; }
			if (n == "entered") { out = EV(now - enteredAt); return true; }
			if (n == "dir") { out = EV((double)moveDir); return true; }
			if (n == "systems.count" || n == "systemCount") { out = EV((double)systems.size()); return true; }
			if (n == "games.count" || n == "gameCount") { out = EV((double)games.size()); return true; }
			if (n == "launch.t") { out = EV(launchAt < 0 ? 0.0 : now - launchAt); return true; }
			if (n == "launch.progress") { out = EV(launchAt < 0 ? 0.0 : std::min(1.0, (now - launchAt) / std::max(1.0, launchMs))); return true; }
			if (n == "empty") { out = EV(m->mView != rnds::UI::HOME && games.empty() ? 1.0 : 0.0); return true; }
			if (n == "empty.title") { out = EV(m->src->emptyTitle()); return true; }
			if (n == "empty.hint") { out = EV(m->src->emptyHint()); return true; }
			if (pre("sys.", rest)) { if (m->mSys < (int)systems.size()) { if (sysField(systems[m->mSys], m->mSys, rest, out)) return true; } out = EV(""); return true; }
			if (pre("game.", rest))
			{
				if (m->mView != rnds::UI::HOME && m->mGame < (int)games.size() && gameField(games[m->mGame], m->mGame, rest, out)) return true;
				out = EV("");
				return true;
			}
			return false;
		}

		// ---- evaluation ------------------------------------------------------------------------------------------
		struct Ctx
		{
			const Node* n;
			const Scope* sc;
			std::vector<const std::map<std::string, Prop>*> on;	// the states that hold, the last first
			std::string key;	// the element's identity (its node and list indices): its eased values
		};
		Ctx ctx(const Node* n, const Scope* sc, const std::string& key) const
		{
			Ctx c{ n, sc, {}, key };
			for (auto it = n->states.rbegin(); it != n->states.rend(); ++it) if (it->when.eval(*sc).truthy()) c.on.push_back(&it->props);
			return c;
		}
		const Prop* find(const Ctx& c, const std::string& k) const
		{
			for (auto* st : c.on) { auto it = st->find(k); if (it != st->end()) return &it->second; }
			return c.n->prop(k);
		}
		bool has(const Ctx& c, const std::string& k) const { return find(c, k) != nullptr; }
		EV val(const Ctx& c, const std::string& k, const EV& def = EV("")) const
		{
			const Prop* p = find(c, k);
			return p ? p->eval(*c.sc) : def;
		}
		std::string str(const Ctx& c, const std::string& k, const std::string& def = "") const { const Prop* p = find(c, k); return p ? p->eval(*c.sc).str() : def; }
		// a length: a number, or "50%" of the parent's (rel)
		double num(const Ctx& c, const std::string& k, double def, double rel = 0)
		{
			const Prop* p = find(c, k);
			if (!p) return smoothOf(c, k, def);
			EV v = p->eval(*c.sc);
			double r = def;
			if (v.isStr)
			{
				const std::string& t = v.s;
				if (!t.empty() && t.back() == '%') r = atof(t.c_str()) / 100 * rel;
				else if (!t.empty()) r = v.num();
			}
			else r = v.n;
			return smoothOf(c, k, r);
		}
		double smoothOf(const Ctx& c, const std::string& k, double target)
		{
			auto sm = c.n->smooth.find(k);
			if (sm == c.n->smooth.end() || sm->second <= 0) return target;
			Smoothed& st = smoothed[c.key + "." + k];
			if (!st.set) { st.set = true; st.v = target; st.target = target; st.at = now; return target; }
			st.target = target;
			double dt = std::max(0.0, now - st.at);
			st.at = now;
			st.v += (target - st.v) * (1 - std::exp(-dt / sm->second));
			if (std::fabs(target - st.v) < 0.05) st.v = target;
			else smoothMoving = true;
			return st.v;
		}
		bool color(const Ctx& c, const std::string& k, uint32_t& out) const
		{
			const Prop* p = find(c, k);
			if (!p) return false;
			EV v = p->eval(*c.sc);
			if (!v.isStr) { out = 0xff000000 | (uint32_t)v.n; return true; }
			return parseColorText(v.s, out);
		}
		bool flag(const Ctx& c, const std::string& k, bool def = false) const { const Prop* p = find(c, k); return p ? p->eval(*c.sc).truthy() : def; }

		// ---- drawing ---------------------------------------------------------------------------------------------
		struct Frame { float x, y, k; float opacity; int screen; };	// the parent's origin (layout px), scale, opacity

		std::shared_ptr<rnds::Font> font(const std::string& name)
		{
			auto it = fonts.find(name);
			if (it != fonts.end()) return it->second;
			auto f = fontFiles.find(name);
			std::shared_ptr<rnds::Font> r;
			if (f != fontFiles.end()) r = rnds::Font::load(f->second);
			if (!r && name != defaultFont) { err("font \"" + name + "\" can't be loaded (" + (f != fontFiles.end() ? f->second : "not in fonts") + ")"); r = font(defaultFont); }
			if (!r && name != "pixel") r = font("pixel");
			fonts[name] = r;
			return r;
		}

		// text's own size (layout px): the widest line and the lines' height
		struct TextLayout { std::vector<rnds::TextLine> lines; float w = 0, h = 0, lineH = 0, asc = 0; rnds::TextStyle st; };
		TextLayout layoutText(const Ctx& c, float k, float boxW)
		{
			TextLayout L;
			std::string t = str(c, "text");
			L.st.font = font(str(c, "font", defaultFont));
			if (!L.st.font) return L;
			L.st.size = (float)num(c, "size", 16) * k;
			L.st.letterSpacing = (float)num(c, "spacing", 0) * k;
			L.st.upper = flag(c, "upper");
			L.asc = L.st.font->ascent(L.st.size);
			L.lineH = has(c, "lineHeight") ? (float)num(c, "lineHeight", 0) * k : L.st.font->normalLineHeight(L.st.size);
			if (t.empty()) { L.h = 0; return L; }
			std::u32string u = rnds::utf8to32(t);
			int maxLines = (int)num(c, "maxLines", flag(c, "wrap") ? 0 : 1);
			// lines the text itself breaks, each wrapped or cut to the box
			std::vector<std::u32string> paras;
			std::u32string cur;
			for (char32_t ch : u) { if (ch == U'\n') { paras.push_back(cur); cur.clear(); } else cur += ch; }
			paras.push_back(cur);
			for (auto& p : paras)
			{
				if (maxLines > 0 && (int)L.lines.size() >= maxLines) break;
				if (boxW > 0 && (flag(c, "wrap") || maxLines != 1))
				{
					int left = maxLines > 0 ? maxLines - (int)L.lines.size() : 0;
					for (auto& l : rnds::layoutWrap(p, L.st, (int)boxW, left)) L.lines.push_back(l);
				}
				else if (boxW > 0 && flag(c, "ellipsis", true)) L.lines.push_back(rnds::layoutEllipsis(p, L.st, (int)boxW));
				else L.lines.push_back(rnds::layoutLine(p, L.st));
			}
			for (auto& l : L.lines) L.w = std::max(L.w, l.width);
			L.h = L.lines.size() * L.lineH;
			return L;
		}

		void drawLine(const rnds::TextLine& l, const rnds::TextStyle& st, float asc, float x, float y, const rnds::Color& col)
		{
			if (l.text.empty()) return;
			std::string key = std::to_string((uintptr_t)st.font.get()) + "|" + std::to_string(st.size) + "|" + std::to_string(st.letterSpacing) +
				(st.upper ? "U|" : "|") + std::to_string(l.width) + "|";
			for (char32_t ch : l.text) { key += (char)(ch & 0xff); if (ch > 0xff) { key += (char)(ch >> 8); key += (char)(ch >> 16); } }
			auto it = texts.find(key);
			if (it == texts.end())
			{
				// the line drawn at device size: layout px times s, at the size already scaled
				rnds::TextBitmap tb = rnds::renderLine(l, st, s, asc);
				TextTex tt;
				if (!tb.bmp.empty()) tt.tex = m->be->upload(tb.bmp, true);
				tt.padX = tb.padX; tt.padY = tb.padY; tt.w = l.width;
				if (texts.size() > 800) texts.clear();
				it = texts.emplace(key, tt).first;
			}
			const TextTex& tt = it->second;
			if (tt.tex) m->be->draw(tt.tex, std::floor(x * s) - tt.padX, std::floor(y * s) - tt.padY, (float)tt.tex->w, (float)tt.tex->h, col);
		}

		// a rounded box's pixels: fill (a colour or a gradient), border, and a soft shadow, by its signed distance
		rnds::TexPtr shape(int w, int h, const float rad[4], uint32_t fill, const std::vector<std::pair<float, uint32_t>>& stops, float angle,
			float border, uint32_t borderColor, float blur, float spread, uint32_t shadowColor, int& padOut)
		{
			int pad = shadowColor >> 24 ? (int)std::ceil(blur + std::max(0.0f, spread)) + 1 : 0;
			char kb[256];
			snprintf(kb, sizeof kb, "%d|%d|%g,%g,%g,%g|%08x|%g|%g|%08x|%g|%g|%08x", w, h, rad[0], rad[1], rad[2], rad[3], fill, angle, border, borderColor, blur, spread, shadowColor);
			std::string key = kb;
			for (auto& st : stops) { snprintf(kb, sizeof kb, "|%g:%08x", st.first, st.second); key += kb; }
			padOut = pad;
			auto it = shapes.find(key);
			if (it != shapes.end()) return it->second;
			rnds::Bitmap b;
			b.w = w + 2 * pad; b.h = h + 2 * pad;
			if (b.w <= 0 || b.h <= 0 || (size_t)b.w * b.h > 4096u * 4096u) return nullptr;
			b.px.assign((size_t)b.w * b.h * 4, 0);
			float hw = w / 2.0f, hh = h / 2.0f;
			float maxR = std::min(hw, hh);
			// the gradient's direction (CSS: 180deg runs top to bottom)
			float ar = (angle - 90) * 3.14159265f / 180, dx = std::cos(ar), dy = std::sin(ar);
			float span = std::fabs(w * dx) + std::fabs(h * dy);
			auto sdf = [&](float px, float py, float grow)	// px, py from the centre
			{
				float r = std::min(maxR, px < 0 ? (py < 0 ? rad[0] : rad[3]) : (py < 0 ? rad[1] : rad[2]));
				r = std::max(0.0f, r + grow);
				float qx = std::fabs(px) - (hw + grow) + r, qy = std::fabs(py) - (hh + grow) + r;
				float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
				return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
			};
			auto ch = [](uint32_t c, int sh) { return ((c >> sh) & 255) / 255.0f; };
			for (int y = 0; y < b.h; y++)
				for (int x = 0; x < b.w; x++)
				{
					float px = x - pad + 0.5f - hw, py = y - pad + 0.5f - hh;
					float d = sdf(px, py, 0);
					float r = 0, g = 0, bl = 0, a = 0;	// premultiplied while it's built
					auto over = [&](float cr, float cg, float cb, float ca)
					{
						r = cr * ca + r * (1 - ca); g = cg * ca + g * (1 - ca); bl = cb * ca + bl * (1 - ca); a = ca + a * (1 - ca);
					};
					if (pad)
					{
						float ds = sdf(px, py, spread);
						float sa = blur > 0 ? 1 - std::min(1.0f, std::max(0.0f, (ds + blur) / (2 * blur))) : std::min(1.0f, std::max(0.0f, 0.5f - ds));
						sa = sa * sa * (3 - 2 * sa);
						if (sa > 0) over(ch(shadowColor, 16), ch(shadowColor, 8), ch(shadowColor, 0), sa * ch(shadowColor, 24));
					}
					float cov = std::min(1.0f, std::max(0.0f, 0.5f - d));
					if (cov > 0)
					{
						uint32_t fc = fill;
						if (!stops.empty())
						{
							float t = span > 0 ? ((px * dx + py * dy) / span + 0.5f) : 0;
							fc = stops.front().second;
							for (size_t k = 1; k < stops.size(); k++)
							{
								if (t <= stops[k - 1].first) break;
								float t0 = stops[k - 1].first, t1 = stops[k].first;
								float f = t1 > t0 ? std::min(1.0f, (t - t0) / (t1 - t0)) : 1;
								uint32_t c0 = stops[k - 1].second, c1 = stops[k].second;
								auto lp = [&](int sh) { return (uint32_t)std::lround(((c0 >> sh) & 255) + ((float)((c1 >> sh) & 255) - ((c0 >> sh) & 255)) * f) << sh; };
								fc = lp(24) | lp(16) | lp(8) | lp(0);
							}
						}
						if (fc >> 24) over(ch(fc, 16), ch(fc, 8), ch(fc, 0), cov * ch(fc, 24));
						if (border > 0 && borderColor >> 24)
						{
							float bc = cov * std::min(1.0f, std::max(0.0f, 0.5f + d + border));
							if (bc > 0) over(ch(borderColor, 16), ch(borderColor, 8), ch(borderColor, 0), bc * ch(borderColor, 24));
						}
					}
					uint8_t* o = &b.px[((size_t)y * b.w + x) * 4];
					if (a > 0) { o[0] = (uint8_t)std::lround(r / a * 255); o[1] = (uint8_t)std::lround(g / a * 255); o[2] = (uint8_t)std::lround(bl / a * 255); }
					o[3] = (uint8_t)std::lround(a * 255);
				}
			rnds::TexPtr t = m->be->upload(b, false);
			if (shapes.size() > 400) shapes.clear();
			shapes[key] = t;
			return t;
		}

		void drawRect(const Ctx& c, float x, float y, float w, float h, const Frame& f)
		{
			float k = f.k;
			uint32_t fill = 0;
			bool hasFill = color(c, "fill", fill) || color(c, "color", fill);
			std::vector<std::pair<float, uint32_t>> stops;
			for (int i = 0; i < 16; i++)
			{
				std::string key = "gradient.stops." + std::to_string(i);
				if (!has(c, key)) { key = "gradient." + std::to_string(i); if (!has(c, key)) break; }
				std::string t = str(c, key);
				// "#rrggbb" or "#rrggbb 40%"
				size_t sp = t.find(' ');
				uint32_t col = 0;
				if (!parseColorText(sp == std::string::npos ? t : t.substr(0, sp), col)) continue;
				float at = sp == std::string::npos ? -1 : atof(t.c_str() + sp + 1) / 100.0f;
				stops.emplace_back(at, col);
			}
			if (!stops.empty())
			{
				for (size_t i = 0; i < stops.size(); i++) if (stops[i].first < 0) stops[i].first = stops.size() == 1 ? 0 : (float)i / (stops.size() - 1);
				if (stops.size() == 1) { fill = stops[0].second; hasFill = true; stops.clear(); }
			}
			float angle = (float)num(c, "gradient.angle", 180);
			float rad[4];
			double r0 = num(c, "radius", 0, std::min(w, h));
			for (int i = 0; i < 4; i++) rad[i] = (float)(has(c, "radius." + std::to_string(i)) ? num(c, "radius." + std::to_string(i), 0) : r0) * k * s;
			float border = (float)num(c, "border", 0) * k * s;
			if (has(c, "border.width")) border = (float)num(c, "border.width", 0) * k * s;
			uint32_t bc = 0;
			if (!color(c, "borderColor", bc)) color(c, "border.color", bc);
			uint32_t shc = 0;
			color(c, "shadow.color", shc);
			float blur = (float)num(c, "shadow.blur", 0) * k * s, spread = (float)num(c, "shadow.spread", 0) * k * s;
			float sx = (float)num(c, "shadow.x", 0) * k * s, sy = (float)num(c, "shadow.y", 0) * k * s;
			if (!hasFill && stops.empty() && !(border > 0 && bc >> 24) && !(shc >> 24)) return;
			float dx = x * s, dy = y * s, dw = w * s, dh = h * s;
			bool plain = stops.empty() && !(border > 0 && bc >> 24) && !(shc >> 24) && rad[0] <= 0 && rad[1] <= 0 && rad[2] <= 0 && rad[3] <= 0;
			if (plain) { m->be->fill(std::round(dx), std::round(dy), std::round(dx + dw) - std::round(dx), std::round(dy + dh) - std::round(dy), colorFrom(fill, f.opacity)); return; }
			int iw = (int)std::lround(dw), ih = (int)std::lround(dh);
			if (iw <= 0 || ih <= 0) return;
			rnds::Color tint(1, 1, 1, f.opacity);
			if (shc >> 24 && (sx != 0 || sy != 0))
			{
				// the shadow offset: drawn on its own, under the box
				int pad;
				float zero[4] = { rad[0], rad[1], rad[2], rad[3] };
				rnds::TexPtr st = shape(iw, ih, zero, 0, {}, 0, 0, 0, blur, spread, shc, pad);
				if (st) m->be->draw(st, std::round(dx + sx) - pad, std::round(dy + sy) - pad, (float)st->w, (float)st->h, tint);
				shc = 0;
			}
			int pad;
			rnds::TexPtr t = shape(iw, ih, rad, hasFill ? fill : 0, stops, angle, border, bc, blur, spread, shc, pad);
			if (t) m->be->draw(t, std::round(dx) - pad, std::round(dy) - pad, (float)t->w, (float)t->h, tint);
		}

		void drawText(const Ctx& c, float x, float y, float w, float h, const Frame& f, const TextLayout& L)
		{
			if (L.lines.empty() || !L.st.font) return;
			uint32_t col = 0xffffffff;
			color(c, "color", col);
			std::string align = str(c, "align", "left"), valign = str(c, "valign", "top");
			float top = y;
			if (h > 0 && valign == "middle") top = y + (h - L.h) / 2;
			else if (h > 0 && valign == "bottom") top = y + h - L.h;
			uint32_t shc = 0;
			bool shadow = color(c, "shadow.color", shc) && shc >> 24;
			float sx = (float)num(c, "shadow.x", 1) * f.k, sy = (float)num(c, "shadow.y", 1) * f.k;
			uint32_t olc = 0;
			bool outline = color(c, "outline.color", olc) && olc >> 24;
			float ow = (float)num(c, "outline.width", 1) * f.k;
			for (size_t i = 0; i < L.lines.size(); i++)
			{
				const rnds::TextLine& l = L.lines[i];
				float lx = x;
				if (w > 0 && align == "center") lx = x + (w - l.width) / 2;
				else if (w > 0 && align == "right") lx = x + w - l.width;
				float ly = top + i * L.lineH + (L.lineH - L.st.font->normalLineHeight(L.st.size)) / 2;
				if (outline)
					for (int k = 0; k < 8; k++)
					{
						static const int ox[8] = { -1, 0, 1, -1, 1, -1, 0, 1 }, oy[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
						drawLine(l, L.st, L.asc, lx + ox[k] * ow, ly + oy[k] * ow, colorFrom(olc, f.opacity));
					}
				if (shadow) drawLine(l, L.st, L.asc, lx + sx, ly + sy, colorFrom(shc, f.opacity));
				drawLine(l, L.st, L.asc, lx, ly, colorFrom(col, f.opacity));
			}
		}

		std::string imageSrc(const Ctx& c, const std::string& k)
		{
			std::string v = str(c, k);
			if (v.empty() || v.compare(0, 8, "romicon:") == 0 || v[0] == '/') return v;
			// the theme's own files are next to it; a hook's path (relative only in a test run) is the host's
			std::string t = file(v);
			return std::ifstream(t) ? t : v;
		}

		void drawImage(const Ctx& c, float x, float y, float w, float h, const Frame& f)
		{
			std::string src = imageSrc(c, "src");
			if ((src.empty() || failed.count(src)) && has(c, "fallback")) src = imageSrc(c, "fallback");
			if (src.empty() || failed.count(src)) { drawPlaceholder(c, x, y, w, h, f); return; }
			std::string fit = str(c, "fit", "cover"), crop = str(c, "crop");
			int iw = (int)std::lround(w * s), ih = (int)std::lround(h * s);
			if (iw <= 0 || ih <= 0) return;
			float radius = (float)num(c, "radius", 0, std::min(w, h)) * f.k * s;
			char kb[64];
			snprintf(kb, sizeof kb, "|%d|%d|%g", iw, ih, radius);
			std::string key = src + "|" + fit + "|" + crop + kb;
			uint32_t tc = 0xffffffff;
			color(c, "tint", tc);
			rnds::Color tint = colorFrom(tc, f.opacity);
			auto it = images.find(key);
			if (it == images.end())
			{
				// another size of the same picture meanwhile, scaled (an element growing), else the placeholder
				auto lt = latest.find(src);
				if (lt != latest.end()) it = images.find(lt->second);
				std::lock_guard<std::mutex> l(m->jm);
				if (!pending.count(key) && !pendingSrc.count(src))
				{
					pending.insert(key);
					pendingSrc.insert(src);
					jobs.push_back(Job{ key, src, fit, crop, iw, ih, radius });
					while (jobs.size() > 32) { pending.erase(jobs.front().key); pendingSrc.erase(jobs.front().src); jobs.pop_front(); }
					if (m->mQueued) m->mQueued();
				}
				if (it == images.end()) { drawPlaceholder(c, x, y, w, h, f); return; }
				const ImgTex& im = it->second;
				if (!im.tex) return;
				float kx = iw / std::max(1.0f, im.w + 2 * im.ox), ky = ih / std::max(1.0f, im.h + 2 * im.oy);
				m->be->draw(im.tex, std::round(x * s) + im.ox * kx, std::round(y * s) + im.oy * ky, im.w * kx, im.h * ky, tint);
				return;
			}
			const ImgTex& im = it->second;
			if (im.tex) m->be->draw(im.tex, std::round(x * s) + im.ox, std::round(y * s) + im.oy, im.w, im.h, tint);
		}
		void drawPlaceholder(const Ctx& c, float x, float y, float w, float h, const Frame& f)
		{
			uint32_t pc;
			if (color(c, "placeholder", pc) && pc >> 24) m->be->fill(std::round(x * s), std::round(y * s), std::round(w * s), std::round(h * s), colorFrom(pc, f.opacity));
		}

		// an element's box in its parent (layout px, before the frame's scale): x, y, w, h
		struct Box { float x, y, w, h; };
		Box boxOf(const Ctx& c, float pw, float ph, float natW, float natH)
		{
			float w = (float)num(c, "w", -1, pw), h = (float)num(c, "h", -1, ph);
			bool hasX = has(c, "x"), hasR = has(c, "right"), hasY = has(c, "y"), hasB = has(c, "bottom");
			float x = (float)num(c, "x", 0, pw), y = (float)num(c, "y", 0, ph);
			if (w < 0 && hasX && hasR) w = pw - x - (float)num(c, "right", 0, pw);
			if (h < 0 && hasY && hasB) h = ph - y - (float)num(c, "bottom", 0, ph);
			if (w < 0) w = natW >= 0 ? natW : (c.n->type == "text" || c.n->type == "image" ? 0 : pw);
			if (h < 0) h = natH >= 0 ? natH : (c.n->type == "text" || c.n->type == "image" ? 0 : ph);
			if (!hasX && hasR) x = pw - (float)num(c, "right", 0, pw) - w;
			if (!hasY && hasB) y = ph - (float)num(c, "bottom", 0, ph) - h;
			// the anchor: where on the element its x, y are ("center", "bottom", "top right", or ax, ay 0..1)
			float ax = 0, ay = 0;
			std::string an = str(c, "anchor");
			if (!an.empty())
			{
				if (an.find("center") != std::string::npos || an.find("middle") != std::string::npos) { ax = 0.5f; ay = 0.5f; }
				if (an.find("left") != std::string::npos) ax = 0;
				if (an.find("right") != std::string::npos) ax = 1;
				if (an.find("top") != std::string::npos) ay = 0;
				if (an.find("bottom") != std::string::npos) ay = 1;
				if (an == "top" || an == "bottom") ax = 0.5f;
				if (an == "left" || an == "right") ay = 0.5f;
			}
			ax = (float)num(c, "ax", ax); ay = (float)num(c, "ay", ay);
			// "scale": the element grown or shrunk about its anchor point
			float sc = (float)num(c, "scale", 1);
			x -= ax * w * sc; y -= ay * h * sc;
			return { x, y, w * sc, h * sc };
		}

		// a group's children placed one after another ("layout": "row" or "column"): their positions along it
		struct Placed { float dx = 0, dy = 0; bool set = false; };

		// the natural size of an element (text: its lines; groups laid out in a row or column: the sum)
		void natural(const Ctx& c, float pw, float ph, float k, float& nw, float& nh)
		{
			nw = -1; nh = -1;
			if (c.n->type == "text")
			{
				float bw = (float)num(c, "w", 0, pw);
				TextLayout L = layoutText(c, k, bw * k);
				nw = bw > 0 ? bw : L.w / k; nh = L.h / k;
				return;
			}
			// a row or a column: its children's sizes, the gaps and the padding (a pill as wide as its text)
			std::string lay = str(c, "layout");
			if (c.n->type != "group" || (lay != "row" && lay != "column")) return;
			bool row = lay == "row";
			float gap = (float)num(c, "gap", 0), pad = (float)num(c, "padding", 0), along = 0, across = 0;
			int kids = 0;
			for (auto& ch : c.n->children)
			{
				Ctx cc = ctx(ch.get(), c.sc, "");
				if ((has(cc, "visible") && !flag(cc, "visible")) || (has(cc, "if") && !flag(cc, "if"))) continue;
				float kw, kh;
				natural(cc, pw, ph, k, kw, kh);
				kw = (float)num(cc, "w", kw >= 0 ? kw : 0, pw); kh = (float)num(cc, "h", kh >= 0 ? kh : 0, ph);
				along += row ? kw : kh;
				across = std::max(across, row ? kh : kw);
				kids++;
			}
			along += gap * std::max(0, kids - 1) + 2 * pad;
			across += 2 * pad;
			nw = row ? along : across;
			nh = row ? across : along;
		}

		void renderNode(const Node* n, const Scope& sc, const Frame& f, float pw, float ph, const std::string& key)
		{
			Ctx c = ctx(n, &sc, key);
			if (has(c, "visible") && !flag(c, "visible")) return;
			if (has(c, "if") && !flag(c, "if")) return;
			float opacity = f.opacity * (float)std::min(1.0, std::max(0.0, num(c, "opacity", 1)));
			if (opacity <= 0.002f) return;
			// the scope of the children: a component's parameters
			Scope inner;
			inner.parent = &sc;
			for (auto& w : n->with) inner.set(w.first, w.second.eval(sc));
			const Scope& csc = n->with.empty() ? sc : inner;
			if (!n->with.empty()) c.sc = &csc;
			float nw = -1, nh = -1;
			TextLayout L;
			if (n->type == "text")
			{
				float bw = (float)num(c, "w", 0, pw);
				float sc0 = (float)num(c, "scale", 1);
				L = layoutText(c, f.k * sc0, bw * f.k * sc0);
				nw = L.w / (f.k * sc0); nh = L.h / (f.k * sc0);
				if (bw > 0) nw = bw;
			}
			else if (n->type == "group" && (!has(c, "w") || !has(c, "h"))) natural(c, pw, ph, f.k, nw, nh);
			Box b = boxOf(c, pw, ph, nw, nh);
			float ax0 = f.x + b.x * f.k, ay0 = f.y + b.y * f.k, aw = b.w * f.k, ah = b.h * f.k;	// absolute layout px
			if (has(c, "onTap") || has(c, "tap"))
				hits.push_back(Hit{ f.screen, ax0 * s, ay0 * s, (ax0 + aw) * s, (ay0 + ah) * s, "", -1, str(c, has(c, "onTap") ? "onTap" : "tap") });
			Frame cf{ ax0, ay0, f.k * (float)num(c, "scale", 1), opacity, f.screen };
			if (n->type == "rect") drawRect(c, ax0, ay0, aw, ah, cf);
			else if (n->type == "text")
			{
				// scaled text is laid out at its scaled size (sharp, not a stretched bitmap)
				drawText(c, ax0, ay0, has(c, "w") ? aw : 0, has(c, "h") ? ah : 0, cf, L);
			}
			else if (n->type == "image") drawImage(c, ax0, ay0, aw, ah, cf);
			else if (n->type == "group")
			{
				// a group can paint a box behind its children too
				if (has(c, "fill") || has(c, "gradient.stops.0") || has(c, "gradient.0") || has(c, "border") || has(c, "border.width") || has(c, "shadow.color")) drawRect(c, ax0, ay0, aw, ah, cf);
			}
			bool clip = flag(c, "clip");
			if (clip) m->be->pushClip(std::round(ax0 * s), std::round(ay0 * s), std::round(aw * s), std::round(ah * s));
			if (n->type == "list") renderList(c, csc, cf, b.w, b.h, key);
			else if (!n->children.empty())
			{
				std::string lay = str(c, "layout");
				if (lay == "row" || lay == "column") renderFlow(c, csc, cf, b.w, b.h, key, lay == "row");
				else for (size_t i = 0; i < n->children.size(); i++) renderNode(n->children[i].get(), csc, cf, b.w, b.h, key + "/" + std::to_string(i));
			}
			if (clip) m->be->popClip();
		}

		// children one after another: each one's size from its own w/h (or its text), gap between, justify along and
		// align across the group
		void renderFlow(const Ctx& c, const Scope& sc, const Frame& f, float w, float h, const std::string& key, bool row)
		{
			float gap = (float)num(c, "gap", 0), pad = (float)num(c, "padding", 0);
			std::string justify = str(c, "justify", "start"), align = str(c, "align", "start");
			struct Kid { const Node* n; float w, h; };
			std::vector<Kid> kids;
			float total = 0;
			for (auto& ch : c.n->children)
			{
				Ctx cc = ctx(ch.get(), &sc, "");
				if ((has(cc, "visible") && !flag(cc, "visible")) || (has(cc, "if") && !flag(cc, "if"))) continue;
				float nw, nh;
				natural(cc, w, h, f.k, nw, nh);
				float kw = (float)num(cc, "w", nw >= 0 ? nw : 0, w), kh = (float)num(cc, "h", nh >= 0 ? nh : 0, h);
				kids.push_back({ ch.get(), kw, kh });
				total += row ? kw : kh;
			}
			if (kids.empty()) return;
			total += gap * (kids.size() - 1);
			float avail = (row ? w : h) - 2 * pad, at = pad, extra = 0;
			if (justify == "center") at = pad + (avail - total) / 2;
			else if (justify == "end") at = pad + avail - total;
			else if (justify == "between" && kids.size() > 1) extra = (avail - total) / (kids.size() - 1);
			for (size_t i = 0; i < kids.size(); i++)
			{
				const Kid& k = kids[i];
				float cross = row ? h - 2 * pad - k.h : w - 2 * pad - k.w, off = pad;
				if (align == "center") off = pad + cross / 2;
				else if (align == "end") off = pad + cross;
				Frame kf = f;
				kf.x = f.x + (row ? at : off) * f.k;
				kf.y = f.y + (row ? off : at) * f.k;
				renderNode(k.n, sc, kf, k.w, k.h, key + "/" + std::to_string(i));
				at += (row ? k.w : k.h) + gap + extra;
			}
		}

		void renderList(const Ctx& c, const Scope& sc, const Frame& f, float w, float h, const std::string& key)
		{
			const Node* tpl = c.n->item.get();
			if (!tpl) { if (c.n->children.empty()) return; tpl = c.n->children[0].get(); }
			std::string source = str(c, "source", "systems");
			int n = 0, cur = -1;
			std::vector<std::pair<std::string, std::string>> hl;
			if (source == "systems") { n = (int)systems.size(); cur = m->mSys; }
			else if (source == "games") { n = (int)games.size(); cur = m->mView == rnds::UI::HOME ? -1 : m->mGame; }
			else if (source == "hints") { hl = hintsNow(); n = (int)hl.size(); }
			else if (source == "range") n = (int)num(c, "count", 0);
			else { err("list source \"" + source + "\": systems, games, hints or range"); return; }
			std::string dirn = str(c, "direction", "free");
			float itemW = (float)num(c, "itemW", dirn == "column" ? w : 100), itemH = (float)num(c, "itemH", dirn == "row" ? h : 40);
			float gap = (float)num(c, "gap", 0);
			int cols = std::max(1, (int)num(c, "columns", 1));
			float stepX = (float)num(c, "stepX", itemW + gap), stepY = (float)num(c, "stepY", itemH + gap);
			std::string mode = str(c, "scrollMode", "center");
			bool loop = flag(c, "loop") && n > 0;
			// the list the cursor is on scrolls with its eased position; another one stands at its selection
			bool active = (source == "systems" && m->mView == rnds::UI::HOME) || (source == "games" && m->mView != rnds::UI::HOME);
			float pos0 = active ? scroll : (float)std::max(0, cur);
			// the scroll along the list (items, or rows of a grid): "center" keeps the selection in the middle,
			// "edge" scrolls only when it would leave the box, "none" doesn't
			float along = dirn == "row" ? w : h, step = dirn == "row" ? stepX : stepY;
			int lineOfCur = std::max(0, cur) / (dirn == "grid" ? cols : 1);
			float posLine = dirn == "grid" ? pos0 / cols : pos0;
			int visible = std::max(1, (int)std::floor((along + gap) / std::max(1.0f, step)));
			float first = 0;
			if (mode == "center") first = posLine - (along - (step - gap)) / 2 / std::max(1.0f, step);
			else if (mode == "edge")
			{
				Smoothed& st = smoothed[key + ".first"];
				double target = st.set ? st.target : std::max(0, lineOfCur - visible + 1);
				if (lineOfCur < target) target = lineOfCur;
				if (lineOfCur > target + visible - 1) target = lineOfCur - visible + 1;
				double dt = st.set ? std::max(0.0, now - st.at) : 0;
				if (!st.set) { st.set = true; st.v = target; }
				st.target = target;
				st.at = now;
				st.v += (st.target - st.v) * (1 - std::exp(-dt / std::max(1.0, scrollMs)));
				if (std::fabs(st.v - st.target) < 0.002) st.v = st.target; else smoothMoving = true;
				first = (float)st.v;
			}
			int lines = dirn == "grid" ? (n + cols - 1) / cols : n;
			// the items in view (and one more each side); a free list draws those within "range" of the cursor
			int lo = 0, hi = n - 1;
			if (dirn == "free") { int r = (int)num(c, "range", 8); int cc = std::max(0, cur); lo = cc - r; hi = cc + r; if (!loop) { lo = std::max(0, lo); hi = std::min(n - 1, hi); } }
			else
			{
				int l0 = (int)std::floor(first) - 1, l1 = (int)std::ceil(first + along / std::max(1.0f, step)) + 1;
				if (!loop) { l0 = std::max(0, l0); l1 = std::min(lines - 1, l1); }
				lo = dirn == "grid" ? l0 * cols : l0;
				hi = dirn == "grid" ? l1 * cols + cols - 1 : l1;
				if (!loop) hi = std::min(n - 1, hi);
			}
			if (n == 0) return;
			std::vector<int> order;
			for (int i = lo; i <= hi; i++) order.push_back(i);
			// the selected one drawn last (over its neighbours) unless the theme says otherwise
			if (flag(c, "selectedOnTop", true) && cur >= 0) std::stable_sort(order.begin(), order.end(), [&](int a, int b) { auto idx = [&](int v) { return ((v % n) + n) % n; }; return (idx(a) == cur) < (idx(b) == cur); });
			// an item's scope: its index, whether it's the selection, where it is from the cursor, its own fields
			auto fill = [&](Scope& is, int v, int i)
			{
				is.parent = &sc;
				float rel = (float)(v - std::max(0, cur)), pos = (float)v - pos0;
				if (loop && n > 1)
				{
					// the shortest way round
					float half = n / 2.0f;
					while (pos > half) pos -= n;
					while (pos < -half) pos += n;
					while (rel > half) rel -= n;
					while (rel < -half) rel += n;
				}
				is.vars.reserve(12);
				is.set("i", EV((double)i));
				is.set("n", EV((double)n));
				is.set("sel", EV(i == cur ? 1.0 : 0.0));
				is.set("rel", EV((double)rel));
				is.set("pos", EV((double)pos));
				is.set("first", EV(i == 0 ? 1.0 : 0.0));
				is.set("last", EV(i == n - 1 ? 1.0 : 0.0));
				if (source == "hints") { is.set("item.key", EV(hl[i].first)); is.set("item.label", EV(hl[i].second)); }
				else if (source == "systems") { const rnds::SysInfo* si = &systems[i]; int idx = i; is.resolve = [this, si, idx](const std::string& k, EV& o) { return k.compare(0, 5, "item.") == 0 && sysField(*si, idx, k.substr(5), o); }; }
				else if (source == "games") { const rnds::GameInfo* gi = &games[i]; int idx = i; is.resolve = [this, gi, idx](const std::string& k, EV& o) { return k.compare(0, 5, "item.") == 0 && gameField(*gi, idx, k.substr(5), o); }; }
			};
			// "flow": each item as wide (row) or high (column) as it is, one after another (hints of different lengths)
			std::vector<float> flowAt;
			if (flag(c, "flow") && (dirn == "row" || dirn == "column"))
			{
				float at = 0;
				for (int i = 0; i < n; i++)
				{
					flowAt.push_back(at);
					Scope is;
					fill(is, i, i);
					Ctx tc = ctx(tpl, &is, "");
					float nw, nh;
					natural(tc, itemW, itemH, f.k, nw, nh);
					at += (float)(dirn == "row" ? num(tc, "w", nw >= 0 ? nw : itemW, itemW) : num(tc, "h", nh >= 0 ? nh : itemH, itemH)) + gap;
				}
			}
			for (int v : order)
			{
				int i = ((v % n) + n) % n;
				Scope is;
				fill(is, v, i);
				std::string ik = key + "@" + std::to_string(i);
				// where the item's box goes
				float ix = 0, iy = 0;
				if (dirn == "row") ix = (v - first) * stepX;
				else if (dirn == "column") iy = (v - first) * stepY;
				else if (dirn == "grid") { int row = (int)std::floor((float)v / cols); ix = (v - row * cols) * stepX; iy = (row - first) * stepY; }
				if (dirn == "row" && mode == "none") ix = v * stepX;
				if (dirn == "column" && mode == "none") iy = v * stepY;
				if (!flowAt.empty()) { if (dirn == "row") ix = flowAt[i]; else iy = flowAt[i]; }
				is.set("ix", EV((double)ix));
				is.set("iy", EV((double)iy));
				Frame itf = f;
				itf.x = f.x + ix * f.k; itf.y = f.y + iy * f.k;
				float iw = dirn == "free" ? w : itemW, ih = dirn == "free" ? h : itemH;
				size_t before = hits.size();
				renderNode(tpl, is, itf, iw, ih, ik);
				// the item can be tapped: its box (a free list's: the template's root box)
				if (source != "range" && hits.size() == before)
				{
					Ctx tc = ctx(tpl, &is, ik);
					float nw, nh;
					natural(tc, iw, ih, f.k, nw, nh);
					Box tb = boxOf(tc, iw, ih, nw, nh);
					float x0 = itf.x + tb.x * f.k, y0 = itf.y + tb.y * f.k;
					hits.push_back(Hit{ f.screen, x0 * s, y0 * s, (x0 + tb.w * f.k) * s, (y0 + tb.h * f.k) * s, source, i,
						source == "hints" ? "key:" + hl[i].first : "" });
				}
			}
		}

		std::vector<std::pair<std::string, std::string>> hintsNow()
		{
			std::string v = m->mView == rnds::UI::HOME ? "home" : "library";
			auto r = hintSets[v];
			if (extraHints)
				for (auto& h : m->src->hints(m->mView != rnds::UI::HOME, m->mSys, m->mGame))
					if (std::none_of(r.begin(), r.end(), [&](const std::pair<std::string, std::string>& e) { return e.first == h.first; })) r.push_back(h);
			return r;
		}

		const std::vector<NodePtr>* screenFor(const std::string& view, const char* side) const
		{
			for (const std::string& v : { view, std::string(view == "launch" ? "library" : "home"), std::string("home") })
			{
				auto it = screens.find(v + "." + side);
				if (it != screens.end() && !it->second.empty()) return &it->second;
			}
			return nullptr;
		}

		void render()
		{
			hits.clear();
			usedTime = false;
			smoothMoving = false;
			Scope root;
			root.resolve = [this](const std::string& n, EV& out) { return hook(n, out); };
			Scope vs;
			vs.parent = &root;
			for (auto& v : vars) vs.set(v.first, v.second.eval(vs));
			for (int screen = 0; screen < 2; screen++)
			{
				const char* side = screen == 0 ? "top" : "bottom";
				m->be->beginScreen(screen);
				Scope ss;
				ss.parent = &vs;
				ss.set("screen", EV((double)screen));
				Frame f{ 0, 0, 1, 1, screen };
				if (const auto* nodes = screenFor(viewName(), side))
					for (size_t i = 0; i < nodes->size(); i++) renderNode((*nodes)[i].get(), ss, f, W, H, std::string(side) + std::to_string(i));
				auto all = screens.find(std::string("all.") + side);
				if (all != screens.end())
					for (size_t i = 0; i < all->second.size(); i++) renderNode(all->second[i].get(), ss, f, W, H, std::string("all.") + side + std::to_string(i));
				if (debug && screen == 0 && !m->mErrors.empty()) drawErrors();
				m->be->endScreen();
			}
			lastFrame = monoMs();
		}

		void drawErrors()
		{
			auto f = font("pixel");
			if (!f) return;
			m->be->fill(0, 0, W * s, std::min(H, 16.0f + 14 * m->mErrors.size()) * s, rnds::Color(0.3f, 0, 0, 0.85f));
			rnds::TextStyle st; st.font = f; st.size = 12;
			for (size_t i = 0; i < m->mErrors.size() && i < 30; i++)
				drawLine(rnds::layoutEllipsis(rnds::utf8to32(m->mErrors[i]), st, (int)W - 16), st, f->ascent(12), 8, 4 + 14 * i, rnds::Color(1, 0.85f, 0.85f, 1));
		}

		// ---- the picture worker --------------------------------------------------------------------------------------
		static rnds::Bitmap roundCorners(rnds::Bitmap b, float r)
		{
			if (r <= 0 || b.empty() || b.alpha) return b;
			rnds::Bitmap mask = rnds::paintMask(b.w, b.h, r);
			for (size_t i = 0; i < (size_t)b.w * b.h; i++) b.px[i * 4 + 3] = (uint8_t)(b.px[i * 4 + 3] * mask.px[i] / 255);
			return b;
		}
		bool work()
		{
			Job j;
			{
				std::lock_guard<std::mutex> l(m->jm);
				if (jobs.empty()) return false;
				j = jobs.back();	// the newest first: what the cursor is on now
				jobs.pop_back();
			}
			rnds::Bitmap src = j.src.compare(0, 8, "romicon:") == 0 ? rnds::romIcon(j.src.substr(8)) : rnds::decodeFile(j.src);
			ImgTex it;
			if (!src.empty())
			{
				if (j.crop == "cardLabel") src = rnds::cardLabelArt(src);
				else if (j.crop == "noStrip") src = rnds::coverWithoutStrip(src);
				rnds::Bitmap out;
				float ox = 0, oy = 0;
				if (j.fit == "contain" || j.fit == "pixelContain")
				{
					float fw, fh;
					rnds::containSize(src.w, src.h, j.w, j.h, fw, fh);
					int iw = std::max(1, (int)std::lround(fw)), ih = std::max(1, (int)std::lround(fh));
					if (j.fit == "pixelContain")
					{
						// whole multiples of the pixels when it's grown
						int k = std::max(1, std::min(j.w / std::max(1, src.w), j.h / std::max(1, src.h)));
						if (k >= 1 && src.w * k <= j.w && src.h * k <= j.h) { iw = src.w * k; ih = src.h * k; }
						out = rnds::resizeNearest(src, iw, ih);
					}
					else out = rnds::resize(src, iw, ih);
					ox = (j.w - iw) / 2.0f; oy = (j.h - ih) / 2.0f;
				}
				else if (j.fit == "fill") out = rnds::resize(src, j.w, j.h);
				else if (j.fit == "pixel") out = rnds::resizeNearest(src, j.w, j.h);
				else if (j.fit == "coverTop") out = rnds::coverTop(src, j.w, j.h);
				else out = rnds::cover(src, j.w, j.h);
				out = roundCorners(out, j.radius);
				it.tex = m->be->upload(out, false);
				it.ox = std::round(ox); it.oy = std::round(oy); it.w = (float)out.w; it.h = (float)out.h;
			}
			{
				std::lock_guard<std::mutex> l(m->jm);
				done.emplace_back(j.key, it);
				if (src.empty()) failed.insert(j.src);
			}
			if (m->mFinished) m->mFinished();
			return true;
		}

		void take()
		{
			std::lock_guard<std::mutex> l(m->jm);
			for (auto& dn : done)
			{
				pending.erase(dn.first);
				std::string src = dn.first.substr(0, dn.first.find('|'));
				// (the src itself may hold '|': the key's own last four fields are what follow it)
				size_t cut = dn.first.size();
				for (int k = 0; k < 5 && cut != std::string::npos; k++) cut = dn.first.rfind('|', cut - 1);
				if (cut != std::string::npos) src = dn.first.substr(0, cut);
				pendingSrc.erase(src);
				if (!dn.second.tex) continue;
				images[dn.first] = dn.second;
				latest[src] = dn.first;
				imageOrder.push_back(dn.first);
			}
			done.clear();
			while (images.size() > 160 && !imageOrder.empty())
			{
				images.erase(imageOrder.front());
				imageOrder.pop_front();
			}
		}
	};

	// ---- the menu ----------------------------------------------------------------------------------------------------
	LayoutMenu::LayoutMenu(rnds::Source* src_, rnds::Backend* be_, const std::string& path, float scale,
		const std::string& pixelFonts, const std::string& interFonts)
		: d(new Impl), src(src_), be(be_)
	{
		d->m = this;
		d->s = scale;
		d->path = path;
		d->dir = path.substr(0, path.rfind('/'));
		d->load(pixelFonts, interFonts);
		d->rebuild();
	}
	LayoutMenu::~LayoutMenu() {}

	void LayoutMenu::setLog(std::function<void(const std::string&)> log)
	{
		mLog = log;
	}

	void LayoutMenu::setHome(int sys)
	{
		mView = rnds::UI::HOME;
		mSys = sys;
		d->rebuild();
		d->scroll = (float)mSys;
		d->enteredAt = d->now;
		d->launchAt = -1;
	}

	void LayoutMenu::setLibrary(int sys, int game)
	{
		mView = rnds::UI::LIBRARY;
		mSys = sys;
		mGame = game;
		d->rebuild();
		d->scroll = (float)mGame;
		d->enteredAt = d->now;
		d->launchAt = -1;
	}

	void LayoutMenu::dataChanged()
	{
		d->rebuild();
		d->texts.clear();
	}

	namespace
	{
		const char* buttonName(rnds::UI::Button b)
		{
			switch (b)
			{
			case rnds::UI::LEFT: return "left";
			case rnds::UI::RIGHT: return "right";
			case rnds::UI::UP: return "up";
			case rnds::UI::DOWN: return "down";
			case rnds::UI::A: return "a";
			case rnds::UI::B: return "b";
			case rnds::UI::X: return "x";
			case rnds::UI::Y: return "y";
			case rnds::UI::L: return "l";
			case rnds::UI::R: return "r";
			}
			return "";
		}
	}

	bool LayoutMenu::press(Button b)
	{
		if (d->launchAt >= 0) return false;
		std::string v = mView == rnds::UI::HOME ? "home" : "library";
		auto& map = d->input[v];
		auto it = map.find(buttonName(b));
		if (it == map.end()) return false;
		std::string act = it->second, arg;
		size_t colon = act.find(':');
		if (colon != std::string::npos) { arg = act.substr(colon + 1); act = act.substr(0, colon); }
		int n = d->count(), c = d->cursor();
		auto move = [&](int to)
		{
			if (n == 0) return false;
			if (d->wrap) to = ((to % n) + n) % n;
			else to = std::min(std::max(0, to), n - 1);
			if (to == c) return false;
			d->moveDir = to > c ? 1 : -1;
			if (d->wrap && std::abs(to - c) > n / 2) d->moveDir = -d->moveDir;
			if (mView == rnds::UI::HOME) { mSys = to; src->selectSystem(to); }
			else { mGame = to; src->selectGame(to); }
			// (a wrap's jump: the eased scroll goes the short way, as if it came from beside)
			if (d->wrap && std::abs(to - c) > n / 2) d->scroll += to > c ? n : -n;
			d->movedAt = d->now;
			return true;
		};
		auto launch = [&](int game)
		{
			d->launchGame = game;
			d->launchAt = d->now;
			d->cuesDone = 0;
			mView = rnds::UI::LAUNCH;
			d->enteredAt = d->now;
		};
		int k = arg.empty() ? 1 : std::max(1, atoi(arg.c_str()));
		if (act == "none") return false;
		if (act == "prev") return move(c - k);
		if (act == "next") return move(c + k);
		if (act == "first") return move(0);
		if (act == "last") return move(n - 1);
		if (mView == rnds::UI::HOME)
		{
			if (n == 0) return false;
			if (act == "open")
			{
				if (d->systems[c].app) { int g = src->openLibrary(c); mGame = g; d->rebuild(); launch(g); return true; }
				mGame = src->openLibrary(c);
				mView = rnds::UI::LIBRARY;
				d->rebuild();
				d->scroll = (float)mGame;
				d->enteredAt = d->now; d->moveDir = 1;
				return true;
			}
			if (act == "resume")
			{
				int g = src->resumeGame(c);
				if (g < 0) return false;
				mView = rnds::UI::LIBRARY;
				mGame = g;
				d->rebuild();
				d->scroll = (float)g;
				launch(g);
				return true;
			}
			if (act == "fav") { src->toggleSystemFav(c); d->rebuild(); return true; }
			return false;
		}
		if (act == "open" || act == "launch") { if (n == 0) return false; launch(c); return true; }
		if (act == "back")
		{
			src->backToHome();
			mView = rnds::UI::HOME;
			d->rebuild();
			d->scroll = (float)mSys;
			d->enteredAt = d->now; d->moveDir = -1;
			return true;
		}
		if (act == "fav") { if (n == 0) return false; src->toggleGameFav(c); d->rebuild(); return true; }
		if (act == "prevSystem" || act == "nextSystem")
		{
			int ns = (int)d->systems.size(), dd = act == "nextSystem" ? 1 : -1, to = mSys;
			for (int i = 0; i < ns; i++) { to = (to + dd + ns) % ns; if (!d->systems[to].app) break; }
			if (to == mSys) return false;
			src->backToHome();
			mSys = to;
			src->selectSystem(to);
			mGame = src->openLibrary(to);
			d->rebuild();
			d->scroll = (float)mGame;
			d->enteredAt = d->now; d->moveDir = dd;
			return true;
		}
		return false;
	}

	bool LayoutMenu::touch(int screen, float x, float y, bool down)
	{
		if (d->launchAt >= 0) return false;
		if (down) { mDown = true; downX = x; downY = y; downScreen = screen; return false; }
		if (!mDown) return false;
		mDown = false;
		if (screen != downScreen || std::fabs(x - downX) > 24 * d->s || std::fabs(y - downY) > 24 * d->s) return false;
		// the topmost thing under the finger: the last drawn
		for (auto it = d->hits.rbegin(); it != d->hits.rend(); ++it)
		{
			const Impl::Hit& h = *it;
			if (h.screen != screen || x < h.x0 || x >= h.x1 || y < h.y0 || y >= h.y1) continue;
			std::string act = h.action;
			if (act.compare(0, 4, "key:") == 0) act = act.substr(4);
			if (!act.empty())
			{
				std::string a = act;
				for (auto& ch : a) ch = (char)tolower((unsigned char)ch);
				static const std::map<std::string, Button> btn = { { "a", rnds::UI::A }, { "b", rnds::UI::B }, { "x", rnds::UI::X }, { "y", rnds::UI::Y },
					{ "l", rnds::UI::L }, { "r", rnds::UI::R }, { "left", rnds::UI::LEFT }, { "right", rnds::UI::RIGHT }, { "up", rnds::UI::UP }, { "down", rnds::UI::DOWN } };
				auto b = btn.find(a);
				if (b != btn.end()) return press(b->second);
				continue;
			}
			bool homeList = h.source == "systems" && mView == rnds::UI::HOME, gameList = h.source == "games" && mView == rnds::UI::LIBRARY;
			if (!homeList && !gameList) continue;
			int c = d->cursor();
			if (h.index == c) return press(rnds::UI::A);
			if (homeList) { mSys = h.index; src->selectSystem(h.index); }
			else { mGame = h.index; src->selectGame(h.index); }
			d->moveDir = h.index > c ? 1 : -1;
			d->movedAt = d->now;
			return true;
		}
		return false;
	}

	bool LayoutMenu::touchMove(int, float, float) { return false; }

	void LayoutMenu::update(double t)
	{
		double dt = d->last > 0 ? std::max(0.0, t - d->last) : 16;	// (a long gap: nothing moved meanwhile, it arrives)
		d->last = t;
		d->now = t;
		if (d->start < 0) d->start = t;
		d->take();
		d->clock = src->clockText();
		d->date = src->dateText();
		float to = (float)d->cursor();
		float k = 1 - (float)std::exp(-dt / std::max(1.0, d->scrollMs));
		d->scroll += (to - d->scroll) * k;
		if (std::fabs(to - d->scroll) < 0.002f) d->scroll = to;
		if (d->launchAt >= 0)
		{
			while (d->cuesDone < d->cues.size() && t - d->launchAt >= d->cues[d->cuesDone].second) src->cue(d->cues[d->cuesDone++].first);
			if (t - d->launchAt >= d->launchMs)
			{
				d->launchAt = -1;
				mView = rnds::UI::LIBRARY;
				src->launch(d->launchGame);	// the main loop starts it (the library's pending launch)
			}
		}
	}

	void LayoutMenu::render() { d->render(); }

	bool LayoutMenu::animating() const
	{
		return std::fabs(d->scroll - (float)d->cursor()) > 0.002f || d->launchAt >= 0 || d->smoothMoving ||
			d->now - std::max(d->movedAt, d->enteredAt) < d->settleMs;
	}

	int LayoutMenu::nextWake() const
	{
		if (animating()) return 0;
		{
			std::lock_guard<std::mutex> l(jm);
			if (!d->done.empty()) return 0;
		}
		if (d->usedTime) return std::max(0, (int)(d->lastFrame + 1000.0 / d->fps - monoMs()));
		return -1;
	}

	bool LayoutMenu::workOnce() { return d->work(); }
}
