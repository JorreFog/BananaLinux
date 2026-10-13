// banana-scrape: box art, title screens, genres and years for the games on the card, from libretro's free
// collections (no account): libretro-thumbnails (thumbnails.libretro.com: <System>/Named_Boxarts and Named_Titles,
// PNGs named after the No-Intro/Redump names) and libretro-database's metadat (genre and releaseyear DATs). Written
// where the menu's library looks (library.cpp): <system folder>/media/box/<rom name>.jpg (the cover),
// media/label/<rom name>.jpg (the title screen, the cartridge label's art), and the genre and year as a game entry in
// the folder's gamelist.xml (added to one that is there; entries already in it are left alone).
//   banana-scrape [--force]     --force fetches again what is already there
// Progress in /run/banana/scrape.status, one line: "running <done> <total> <system name>|<game>", then
// "done <found> <total>" or "error <why>". A game whose name doesn't match any of the collection's is skipped.
// Names are matched on letters and digits only, the bracketed tags left out, preferring the file's own region.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "../third_party/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace
{
	// ---- libcurl, dlopen()ed as banana-ui does it ------------------------------------------------------------------
	struct Curl
	{
		void* (*init)(void) = nullptr;
		int (*setopt)(void*, int, ...) = nullptr;
		int (*perform)(void*) = nullptr;
		int (*getinfo)(void*, int, ...) = nullptr;
		void (*cleanup)(void*) = nullptr;
		char* (*escape)(void*, const char*, int) = nullptr;
		void (*freep)(void*) = nullptr;
		bool load()
		{
			void* l = dlopen("libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
			if (!l) return false;
			init = (void* (*)(void))dlsym(l, "curl_easy_init");
			setopt = (int (*)(void*, int, ...))dlsym(l, "curl_easy_setopt");
			perform = (int (*)(void*))dlsym(l, "curl_easy_perform");
			getinfo = (int (*)(void*, int, ...))dlsym(l, "curl_easy_getinfo");
			cleanup = (void (*)(void*))dlsym(l, "curl_easy_cleanup");
			escape = (char* (*)(void*, const char*, int))dlsym(l, "curl_easy_escape");
			freep = (void (*)(void*))dlsym(l, "curl_free");
			return init && setopt && perform && getinfo && cleanup && escape && freep;
		}
	} cu;
	size_t onBody(char* p, size_t sz, size_t n, void* u) { ((std::string*)u)->append(p, sz * n); return sz * n; }
	// the body of a GET, "" unless it answered 200. One handle per thread, kept: its connection is reused (the
	// thumbnails' server is far and slow to greet; a new TLS connection for every picture took seconds each)
	std::string get(const std::string& url)
	{
		thread_local void* h = nullptr;
		if (!h) h = cu.init();
		if (!h) return "";
		enum { WRITEDATA = 10001, URL = 10002, USERAGENT = 10018, WRITEFUNCTION = 20011, TIMEOUT = 13, NOSIGNAL = 99,
			FOLLOWLOCATION = 52, RESPONSE_CODE = 0x200002 };
		std::string body;
		long status = 0;
		cu.setopt(h, URL, url.c_str());
		cu.setopt(h, USERAGENT, "BananaOS banana-scrape");
		cu.setopt(h, NOSIGNAL, 1L);
		cu.setopt(h, TIMEOUT, 60L);
		cu.setopt(h, FOLLOWLOCATION, 1L);
		cu.setopt(h, WRITEFUNCTION, onBody);
		cu.setopt(h, WRITEDATA, &body);
		bool ok = cu.perform(h) == 0;
		if (ok) cu.getinfo(h, RESPONSE_CODE, &status);
		if (!ok) { cu.cleanup(h); h = nullptr; }	// (a fresh one next time)
		return ok && status == 200 ? body : "";
	}
	std::string esc(const std::string& s)
	{
		void* h = cu.init();
		char* e = cu.escape(h, s.c_str(), (int)s.size());
		std::string r = e ? e : s;
		if (e) cu.freep(e);
		cu.cleanup(h);
		return r;
	}

	// ---- names ----------------------------------------------------------------------------------------------------
	std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
	// letters and digits only, the (tags) and [tags] left out (and &: the thumbnails write it "_"): "Pokemon - HeartGold Version (USA)" and
	// "Pokemon HeartGold Version" are the same game
	std::string norm(const std::string& s)
	{
		std::string r;
		int depth = 0;
		for (size_t i = 0; i < s.size(); i++)
		{
			char c = s[i];
			if (c == '(' || c == '[') { depth++; continue; }
			if (c == ')' || c == ']') { if (depth) depth--; continue; }
			if (depth) continue;
			if (isalnum((unsigned char)c)) r += (char)tolower((unsigned char)c);	// (& is "_" in the thumbnails' names: left out)
		}
		return r;
	}
	// the same name with its words in any order and "the" left out: "The Legend of Zelda Ocarina of Time" and
	// "Legend of Zelda, The - Ocarina of Time", "007 GoldenEye" and "GoldenEye 007" (tried when norm() finds nothing)
	std::string words(const std::string& s)
	{
		std::vector<std::string> w;
		std::string cur;
		int depth = 0;
		for (size_t i = 0; i <= s.size(); i++)
		{
			char c = i < s.size() ? s[i] : ' ';
			if (c == '(' || c == '[') { depth++; continue; }
			if (c == ')' || c == ']') { if (depth) depth--; continue; }
			if (depth) continue;
			if (isalnum((unsigned char)c)) { cur += (char)tolower((unsigned char)c); continue; }
			if (c == '\'') continue;	// (Mario's, Marios)
			if (!cur.empty() && cur != "the") w.push_back(cur);
			cur.clear();
		}
		std::sort(w.begin(), w.end());
		std::string r;
		for (auto& x : w) r += x + " ";
		return r;
	}
	// how well a collection name fits the file's: its region when the file names one, else the usual preference
	int score(const std::string& cand, const std::string& file)
	{
		std::string c = lower(cand), f = lower(file);
		int s = 0;
		for (const char* r : { "usa", "europe", "japan", "world", "australia", "germany", "france", "spain", "italy" })
			if (f.find(r) != std::string::npos && c.find(r) != std::string::npos) s += 10;
		if (c.find("usa") != std::string::npos) s += 4;
		if (c.find("world") != std::string::npos) s += 4;
		if (c.find("europe") != std::string::npos) s += 2;
		for (const char* bad : { "beta", "proto", "demo", "kiosk", "sample", "virtual console", "switch online", "aftermarket", "unl)" })
			if (c.find(bad) != std::string::npos) s -= 20;
		if (c.find("(rev") != std::string::npos) s -= 1;
		return s;
	}
	// the PNG names in an Apache index page (href="...png", %-encoded)
	std::vector<std::string> indexNames(const std::string& html)
	{
		std::vector<std::string> out;
		size_t at = 0;
		while ((at = html.find("href=\"", at)) != std::string::npos)
		{
			at += 6;
			size_t e = html.find('"', at);
			if (e == std::string::npos) break;
			std::string h = html.substr(at, e - at);
			at = e;
			if (h.size() < 5 || lower(h.substr(h.size() - 4)) != ".png" || h.find('/') != std::string::npos) continue;
			std::string d;
			for (size_t i = 0; i < h.size(); i++)
			{
				if (h[i] == '%' && i + 2 < h.size()) { d += (char)strtol(h.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
				else d += h[i];
			}
			d.resize(d.size() - 4);
			for (const char* ent : { "&amp;" }) { size_t p; while ((p = d.find(ent)) != std::string::npos) d.replace(p, 5, "&"); }
			out.push_back(d);
		}
		return out;
	}
	// a metadat DAT's games: name -> the field's value (genre "Role-Playing", releaseyear "2009")
	std::map<std::string, std::string> datField(const std::string& dat, const char* field)
	{
		std::map<std::string, std::string> out;
		std::istringstream in(dat);
		std::string ln, name, key = std::string(field) + " \"";
		while (std::getline(in, ln))
		{
			size_t a = ln.find_first_not_of(" \t");
			if (a == std::string::npos) continue;
			// the game's name: "comment" in libretro-database's metadat, "name" in other DATs
			if (!ln.compare(a, 9, "comment \"")) { size_t e = ln.find('"', a + 9); if (e != std::string::npos) name = ln.substr(a + 9, e - a - 9); }
			else if (!ln.compare(a, 6, "name \"") && name.empty()) { size_t e = ln.find('"', a + 6); if (e != std::string::npos) name = ln.substr(a + 6, e - a - 6); }
			else if (!ln.compare(a, key.size(), key) && !name.empty())
			{
				size_t e = ln.find('"', a + key.size());
				if (e != std::string::npos) { std::string v = ln.substr(a + key.size(), e - a - key.size()); out.emplace(norm(name), v); out.emplace(words(name), v); }
			}
			else if (ln[a] == ')') name.clear();
		}
		return out;
	}

	// ---- pictures: decoded, made no larger than max x max (area average), written as PNG ---------------------------
	bool savePicture(const std::string& data, const std::string& path, int max)
	{
		int w, h, n;
		unsigned char* px = stbi_load_from_memory((const unsigned char*)data.data(), (int)data.size(), &w, &h, &n, 4);
		if (!px) return false;
		double k = std::min(1.0, std::min((double)max / w, (double)max / h));
		int ow = std::max(1, (int)(w * k + 0.5)), oh = std::max(1, (int)(h * k + 0.5));
		std::vector<unsigned char> out((size_t)ow * oh * 4);
		for (int y = 0; y < oh; y++)
		{
			int y0 = y * h / oh, y1 = std::max(y0 + 1, (y + 1) * h / oh);
			for (int x = 0; x < ow; x++)
			{
				int x0 = x * w / ow, x1 = std::max(x0 + 1, (x + 1) * w / ow);
				unsigned s[4] = { 0, 0, 0, 0 }, c = 0;
				for (int yy = y0; yy < y1; yy++)
					for (int xx = x0; xx < x1; xx++, c++)
						for (int i = 0; i < 4; i++) s[i] += px[((size_t)yy * w + xx) * 4 + i];
				for (int i = 0; i < 4; i++) out[((size_t)y * ow + x) * 4 + i] = (unsigned char)(s[i] / c);
			}
		}
		stbi_image_free(px);
		// JPEG (quality 90, over black where the picture had transparency): a PNG's compression took most of a second
		// a picture on these cores, and the JPEG is a third of the size on the card
		for (size_t i = 0; i < out.size(); i += 4) { unsigned a = out[i + 3]; for (int c = 0; c < 3; c++) out[i + c] = (unsigned char)(out[i + c] * a / 255); }
		std::vector<unsigned char> rgb((size_t)ow * oh * 3);
		for (size_t i = 0, j = 0; i < out.size(); i += 4, j += 3) { rgb[j] = out[i]; rgb[j + 1] = out[i + 1]; rgb[j + 2] = out[i + 2]; }
		std::string tmp = path + ".part";
		bool ok = stbi_write_jpg(tmp.c_str(), ow, oh, 3, rgb.data(), 90) != 0;
		return ok && rename(tmp.c_str(), path.c_str()) == 0;
	}

	// ---- the card ---------------------------------------------------------------------------------------------------
	struct System { std::string id, name, folder; std::vector<std::string> exts; };
	const std::map<std::string, std::string> LIBRETRO = {
		{ "nds", "Nintendo - Nintendo DS" }, { "gba", "Nintendo - Game Boy Advance" }, { "gbc", "Nintendo - Game Boy Color" },
		{ "gb", "Nintendo - Game Boy" }, { "nes", "Nintendo - Nintendo Entertainment System" },
		{ "snes", "Nintendo - Super Nintendo Entertainment System" }, { "megadrive", "Sega - Mega Drive - Genesis" },
		{ "psx", "Sony - PlayStation" }, { "n64", "Nintendo - Nintendo 64" } };
	std::vector<System> systems(const std::string& conf)
	{
		std::vector<System> out;
		std::ifstream f(conf);
		std::string ln;
		System* cur = nullptr;
		while (std::getline(f, ln))
		{
			if (ln.empty() || ln[0] == '#') continue;
			if (ln[0] == '[') { out.push_back(System()); cur = &out.back(); cur->id = ln.substr(1, ln.find(']') - 1); cur->folder = cur->id; continue; }
			size_t eq = ln.find('=');
			if (!cur || eq == std::string::npos) continue;
			std::string k = ln.substr(0, eq), v = ln.substr(eq + 1);
			k.erase(k.find_last_not_of(" \t") + 1);
			v.erase(0, v.find_first_not_of(" \t"));
			if (k == "name") cur->name = v;
			else if (k == "folder") cur->folder = v;
			else if (k == "ext") { std::istringstream e(v); std::string x; while (e >> x) cur->exts.push_back(lower(x)); }
		}
		return out;
	}
	void listRoms(const std::string& dir, const std::string& rel, const std::vector<std::string>& exts, std::vector<std::string>& out, int depth)
	{
		DIR* d = opendir(dir.c_str());
		if (!d) return;
		while (dirent* e = readdir(d))
		{
			std::string n = e->d_name;
			if (n.empty() || n[0] == '.' || n == "media") continue;
			std::string p = dir + "/" + n, r = rel.empty() ? n : rel + "/" + n;
			struct stat st;
			if (stat(p.c_str(), &st) != 0) continue;
			if (S_ISDIR(st.st_mode)) { if (depth < 2) listRoms(p, r, exts, out, depth + 1); continue; }
			std::string ln = lower(n);
			for (auto& x : exts)
				if (ln.size() > x.size() + 1 && ln.compare(ln.size() - x.size() - 1, std::string::npos, "." + x) == 0) { out.push_back(r); break; }
		}
		closedir(d);
	}
	std::string xmlEsc(const std::string& s)
	{
		std::string r;
		for (char c : s)
			switch (c) { case '&': r += "&amp;"; break; case '<': r += "&lt;"; break; case '>': r += "&gt;"; break; case '"': r += "&quot;"; break; default: r += c; }
		return r;
	}

	std::mutex mu;
	std::string statusLine;
	void status(const std::string& s)
	{
		std::lock_guard<std::mutex> l(mu);
		if (s == statusLine) return;
		statusLine = s;
		if (getenv("BANANA_SCRAPE_VERBOSE")) fprintf(stderr, "%s\n", s.c_str());
		std::string tmp = "/run/banana/scrape.status.new";
		if (FILE* f = fopen(tmp.c_str(), "w")) { fprintf(f, "%s\n", s.c_str()); fclose(f); rename(tmp.c_str(), "/run/banana/scrape.status"); }
	}
}

int main(int argc, char** argv)
{
	bool force = argc > 1 && !strcmp(argv[1], "--force");
	const char* roms = getenv("BANANA_ROMS") ? getenv("BANANA_ROMS") : "/data/roms";
	std::string conf = getenv("BANANA_SYSTEMS") ? getenv("BANANA_SYSTEMS") :
		access("/data/banana/systems.conf", R_OK) == 0 ? "/data/banana/systems.conf" : "/etc/banana/systems.conf";
	// bananad doesn't put the handheld to sleep for idleness while this runs (its pid in /run/banana/awake.pid)
	if (FILE* f = fopen("/run/banana/awake.pid", "w")) { fprintf(f, "%d\n", (int)getpid()); fclose(f); }
	struct Unlink { ~Unlink() { unlink("/run/banana/awake.pid"); } } awake;
	if (!cu.load()) { status("error libcurl is missing"); return 1; }
	if (get("https://thumbnails.libretro.com/").empty()) { status("error no connection"); return 1; }

	struct Job { System sys; std::string dir, rel, stem, box, title; };
	std::vector<Job> jobs;
	std::map<std::string, std::map<std::string, std::string>> genres, years;	// per system: norm name -> value
	int total = 0;
	for (auto& sys : systems(conf))
	{
		auto lr = LIBRETRO.find(sys.id);
		if (lr == LIBRETRO.end()) continue;
		std::string dir = sys.folder[0] == '/' ? sys.folder : std::string(roms) + "/" + sys.folder;
		std::vector<std::string> rels;
		listRoms(dir, "", sys.exts, rels, 0);
		if (rels.empty()) continue;
		status("running 0 " + std::to_string(total + (int)rels.size()) + " " + sys.name + "|the list of covers");
		// the collection's names, to match ours against
		std::string base = "https://thumbnails.libretro.com/" + esc(lr->second) + "/";
		std::vector<std::string> boxes = indexNames(get(base + "Named_Boxarts/")), titles = indexNames(get(base + "Named_Titles/"));
		std::map<std::string, std::vector<std::string>> byBox, byTitle, byBoxW, byTitleW;
		for (auto& n : boxes) { byBox[norm(n)].push_back(n); byBoxW[words(n)].push_back(n); }
		for (auto& n : titles) { byTitle[norm(n)].push_back(n); byTitleW[words(n)].push_back(n); }
		std::string meta = "https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/";
		genres[sys.id] = datField(get(meta + "genre/" + esc(lr->second) + ".dat"), "genre");
		years[sys.id] = datField(get(meta + "releaseyear/" + esc(lr->second) + ".dat"), "releaseyear");
		auto best = [](const std::vector<std::string>& c, const std::string& stem)
		{
			std::string b;
			int bs = -1000;
			for (auto& n : c) { int s = score(n, stem); if (s > bs) { bs = s; b = n; } }
			return b;
		};
		for (auto& rel : rels)
		{
			Job j;
			j.sys = sys; j.dir = dir; j.rel = rel;
			std::string fname = rel.substr(rel.rfind('/') == std::string::npos ? 0 : rel.rfind('/') + 1);
			j.stem = fname.substr(0, fname.rfind('.'));
			std::string k = norm(j.stem), kw = words(j.stem);
			// the same name; the same words in another order; then, as a last try, one name the start of the other
			// ("Aero the Acro-Bat" and "Aero the Acro-Bat - Rascal Rival Revenge", "Rayman 3 - Hoodlum Havoc" and
			// "Rayman 3"), at least 8 letters, the closest in length
			auto find = [&](std::map<std::string, std::vector<std::string>>& by, std::map<std::string, std::vector<std::string>>& byW) -> const std::vector<std::string>*
			{
				auto a = by.find(k);
				if (a != by.end()) return &a->second;
				auto w = byW.find(kw);
				if (w != byW.end()) return &w->second;
				if (k.size() < 8) return nullptr;
				const std::vector<std::string>* bestV = nullptr;
				size_t bestD = 1000;
				for (auto it = by.lower_bound(k.substr(0, 8)); it != by.end() && it->first.compare(0, 8, k, 0, 8) == 0; ++it)
				{
					const std::string& c = it->first;
					bool pre = c.size() >= 8 && (c.compare(0, std::min(c.size(), k.size()), k, 0, std::min(c.size(), k.size())) == 0);
					if (!pre) continue;
					size_t d = c.size() > k.size() ? c.size() - k.size() : k.size() - c.size();
					if (d < bestD) { bestD = d; bestV = &it->second; }
				}
				return bestV;
			};
			if (auto v = find(byBox, byBoxW)) j.box = base + "Named_Boxarts/" + esc(best(*v, j.stem)) + ".png";
			if (auto v = find(byTitle, byTitleW)) j.title = base + "Named_Titles/" + esc(best(*v, j.stem)) + ".png";
			jobs.push_back(j);
		}
		total += (int)rels.size();
	}

	// the pictures, eight at a time (the time goes to waiting for the server, not the CPU)
	std::atomic<int> next{0}, done{0}, found{0};
	auto worker = [&]
	{
		for (int i; (i = next++) < (int)jobs.size();)
		{
			Job& j = jobs[i];
			std::string boxPath = j.dir + "/media/box/" + j.stem + ".jpg", labelPath = j.dir + "/media/label/" + j.stem + ".jpg";
			// (art from before, as PNG, counts as there)
			std::string boxPng = j.dir + "/media/box/" + j.stem + ".png", labelPng = j.dir + "/media/label/" + j.stem + ".png";
			bool got = false;
			if (!j.box.empty())
			{
				if (!force && (access(boxPath.c_str(), F_OK) == 0 || access(boxPng.c_str(), F_OK) == 0)) got = true;
				else
				{
					mkdir((j.dir + "/media").c_str(), 0755);
					mkdir((j.dir + "/media/box").c_str(), 0755);
					std::string d = get(j.box);
					got = !d.empty() && savePicture(d, boxPath, 512);
				}
			}
			if (!j.title.empty() && (force || (access(labelPath.c_str(), F_OK) != 0 && access(labelPng.c_str(), F_OK) != 0)))
			{
				mkdir((j.dir + "/media").c_str(), 0755);
				mkdir((j.dir + "/media/label").c_str(), 0755);
				std::string d = get(j.title);
				if (!d.empty()) savePicture(d, labelPath, 320);
			}
			if (got) found++;
			int n = ++done;
			status("running " + std::to_string(n) + " " + std::to_string(total) + " " + j.sys.name + "|" + j.stem);
		}
	};
	std::vector<std::thread> pool;
	for (int t = 0; t < 8; t++) pool.emplace_back(worker);
	for (auto& t : pool) t.join();

	// genres and years into each folder's gamelist.xml (new entries only)
	std::map<std::string, std::vector<const Job*>> byDir;
	for (auto& j : jobs) byDir[j.dir].push_back(&j);
	for (auto& kv : byDir)
	{
		std::string path = kv.first + "/gamelist.xml", x;
		{ std::ifstream f(path); std::stringstream ss; ss << f.rdbuf(); x = ss.str(); }
		std::string add;
		for (const Job* j : kv.second)
		{
			if (x.find("<path>./" + xmlEsc(j->rel) + "</path>") != std::string::npos || x.find("<path>" + xmlEsc(j->rel) + "</path>") != std::string::npos) continue;
			std::string k = norm(j->stem), kw = words(j->stem), g, y;
			auto& G = genres[j->sys.id];
			auto& Y = years[j->sys.id];
			auto gi = G.find(k); if (gi == G.end()) gi = G.find(kw); if (gi != G.end()) g = gi->second;
			auto yi = Y.find(k); if (yi == Y.end()) yi = Y.find(kw); if (yi != Y.end()) y = yi->second;
			if (g.empty() && y.empty()) continue;
			add += "\t<game>\n\t\t<path>./" + xmlEsc(j->rel) + "</path>\n";
			if (!g.empty()) add += "\t\t<genre>" + xmlEsc(g) + "</genre>\n";
			if (y.size() == 4) add += "\t\t<releasedate>" + y + "0101T000000</releasedate>\n";
			add += "\t</game>\n";
		}
		if (add.empty()) continue;
		size_t end = x.rfind("</gameList>");
		if (end == std::string::npos) x = "<?xml version=\"1.0\"?>\n<gameList>\n" + add + "</gameList>\n";
		else x.insert(end, add);
		std::string tmp = path + ".new";
		if (FILE* f = fopen(tmp.c_str(), "w")) { fwrite(x.data(), 1, x.size(), f); fclose(f); rename(tmp.c_str(), path.c_str()); }
	}
	sync();
	status("done " + std::to_string(found.load()) + " " + std::to_string(total));
	return 0;
}
