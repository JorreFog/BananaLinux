// banana-ui: the reader, a text file on both panels, read-only (#173). Files opens it on a .txt, .md, .nfo or .log (A
// on the file). The text runs from the top panel on to the bottom one, as a book's two pages: UP and DOWN move it a line, LEFT, RIGHT, L and R the two
// panels' worth, a drag on the bottom panel as far as the finger went; B goes back where it was opened from. The
// place in each file is kept for the session, as Files keeps a folder's (Settings::readAt).
// textfile.h reads the bytes (UTF-8, UTF-16, Windows-1252; line ends, tabs) and wraps the lines; this file draws.
#include "settings.h"
#include "settings_draw.h"
#include "fileops.h"
#include "lang.h"
#include "textfile.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_set>

using rnds::Color;

namespace banana
{
	namespace
	{
		// the most of a file read: a long guide is a few hundred KB; a bigger file (a log left to grow, something that
		// isn't a guide) shows its start, and says so, rather than holding the menu up and its memory
		constexpr size_t CAP = 1 << 20;
		// the text: Pixelify Sans (or the theme's font) at 20 px, a line every 28; 13 lines a panel from these tops
		constexpr float SIZE = 20, PITCH = 28, X0 = 40, WIDTH = 560, TOP_Y = 78, BOT_Y = 30;
		constexpr int PER = 13, VIS = 2 * PER;
	}

	struct Settings::Reader
	{
		std::string path, name;
		// the lines as they are drawn (the file's lines wrapped to the panel): their UTF-8 one after another in body,
		// each one's start in at (and the end after the last); the file's line each one is of (the place kept); the
		// first of the notes after the text (drawn muted: the file cut, or empty)
		std::string body;
		std::vector<uint32_t> at, lineOf;
		int notes = 0;
		int top = 0;
		int count() const { return (int)at.size() - 1; }
		std::string line(int i) const { return body.substr(at[i], at[i + 1] - at[i]); }
		int last() const { return std::max(0, count() - VIS); }
	};

	std::string Settings::openText(const std::string& path, Page back)
	{
		std::string name = files::baseName(path), bytes, why;
		uint64_t size = 0;
		auto t0 = std::chrono::steady_clock::now();
		if (!textfile::read(path, CAP, bytes, size, why)) { if (log) log("text: " + path + " can't be read: " + why); return name + " can't be read: " + why + "."; }
		textfile::Text t = textfile::decode(bytes, size > bytes.size());
		bytes = std::string();
		if (t.binary)
		{
			if (log) log("text: " + path + " isn't text");
			return name + " isn't text: it can't be read here.";
		}
		// characters the menu's font lacks (Chinese, Japanese, Korean...): the CJK pixel font for them, before the lines
		// are measured
		{
			std::unordered_set<char32_t> seen;
			bool lacks = false;
			for (auto& l : t.lines)
			{
				for (char32_t c : l)
					if (c >= 0x80 && seen.insert(c).second && R && !R->hasGlyph(c)) { lacks = true; break; }
				if (lacks) break;
			}
			if (lacks) loadCjkFont();
		}
		auto r = std::make_shared<Reader>();
		r->path = path; r->name = name;
		// the widths as the line is laid out to be drawn (each character's advance followed by the next one's, kerning
		// included): ASCII's pairs kept here, as a guide is mostly that and each one asked for takes the font's lock
		std::vector<int16_t> ascii(128 * 129, -1);
		auto advance = [&](char32_t c, char32_t next) -> int
		{
			if (!R) return 10;
			if (c < 128 && next < 128)
			{
				int16_t& a = ascii[c * 129 + next];
				if (a < 0) a = (int16_t)R->advance(c, next, SIZE);
				return a;
			}
			return R->advance(c, next, SIZE);
		};
		auto add = [&](const std::u32string& l, uint32_t from)
		{
			for (auto& pc : textfile::wrap(l, (int)WIDTH, advance))
			{
				r->at.push_back((uint32_t)r->body.size());
				r->lineOf.push_back(from);
				r->body += textfile::utf8(l, pc.first, pc.second);
			}
		};
		for (size_t i = 0; i < t.lines.size(); i++) add(t.lines[i], (uint32_t)i);
		r->notes = (int)r->at.size();
		// what isn't the file's own text, after it (in the menu's language, drawn as the text is)
		std::vector<std::string> notes;
		if (t.lines.empty()) notes.push_back(lang::tr("This file is empty."));
		if (size > CAP) notes.push_back(lang::tr("The file goes on: only its first " + files::bytes(CAP) + " of " + files::bytes(size) + " is shown here."));
		for (auto& n : notes)
		{
			if (!r->at.empty()) add(U"", (uint32_t)t.lines.size());	// (a line between)
			add(rnds::utf8to32(n), (uint32_t)t.lines.size());
		}
		r->at.push_back((uint32_t)r->body.size());
		// (the place kept is the file's line: the first drawn line of it)
		{
			// (a file shorter now than where it was left: its end)
			int want = readAt(path);
			r->top = want > 0 ? r->last() : 0;
			for (int i = 0; i < r->notes && want > 0; i++) if ((int)r->lineOf[i] >= want) { r->top = i; break; }
			r->top = std::min(r->top, r->last());
		}
		// (how long the reading and the wrap took: a guide of a megabyte on the handheld's cores)
		long ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
		// (one line, as lang-extract.py tells a log's words from the screen's by the call they are in)
		if (log) log("text: " + path + ": " + std::to_string(t.lines.size()) + " lines, " + std::to_string(r->count()) + " on the panels" + (size > CAP ? ", the first " + std::to_string(CAP) + " of " + std::to_string(size) + " bytes" : "") + (t.legacy ? ", not UTF-8" : "") + (r->top > 0 ? ", from its line " + std::to_string(r->lineOf[r->top] + 1) + " (drawn line " + std::to_string(r->top + 1) + ")" : "") + " (" + std::to_string(ms) + " ms)");
		rd = r;
		textBack = back;
		page = TEXT;
		return "";
	}

	Settings::Result Settings::pressText(Key k)
	{
		if (!rd) { page = textBack; return MOVED; }
		Reader& r = *rd;
		int to = r.top;
		switch (k)
		{
		case Key::B:
			// (the place kept: the file's line the first drawn line is of)
			keepReadAt(r.path, r.count() ? (int)r.lineOf[std::min(r.top, r.count() - 1)] : 0);
			page = textBack;
			rd.reset();	// (its lines: a long guide's megabytes)
			return MOVED;
		case Key::UP: to--; break;
		case Key::DOWN: to++; break;
		// a page: both panels' lines, as a book's next two pages (held, it repeats)
		case Key::LEFT: case Key::L: to -= VIS; break;
		case Key::RIGHT: case Key::R: to += VIS; break;
		default: return NONE;
		}
		return textTo(to);
	}

	// the text from that drawn line on (within its ends); NONE when it is there already
	Settings::Result Settings::textTo(int to)
	{
		Reader& r = *rd;
		to = std::max(0, std::min(r.last(), to));
		if (to == r.top) return NONE;
		r.top = to;
		// (the headless test: the line now at the top, as it is drawn)
		if (sync && log) log("text: line " + std::to_string(r.top + 1) + " of " + std::to_string(r.count()) + ": " + r.line(r.top));
		return MOVED;
	}

	std::vector<Settings::Chip> Settings::textChips()
	{
		std::vector<Chip> cs = { { Key::B, "B", "Back" } };
		if (rd && rd->last() > 0) cs.push_back({ Key::R, "L / R", "Page", true });
		return cs;
	}

	Settings::Result Settings::touchText(float cx, float cy)
	{
		if (!rd) return NONE;
		Reader& r = *rd;
		if (downY >= 418 && downY <= 466)
		{
			// (Page from the last one: back to the start, as the Achievements list's)
			Key k = chipAt(textChips(), downX);
			if (k == Key::R && r.top >= r.last()) return textTo(0);
			return k == Key::NONE ? NONE : pressText(k);
		}
		// a drag moves the text as far as the finger went (a line every 28 px); a tap does nothing
		float dy = downY - cy;
		(void)cx;
		if (std::fabs(dy) < 20) return NONE;
		return textTo(r.top + (int)std::lround(dy / PITCH));
	}

	void Settings::renderText()
	{
		if (!rd) return;
		const Reader& r = *rd;
		SPal p = pal(light);
		int n = r.count();
		auto lines = [&](int from, float y0)
		{
			for (int i = 0; i < PER && from + i < n; i++)
			{
				int at = from + i;
				std::string l = r.line(at);
				if (l.empty()) continue;
				// (the file's words as they are, never looked up in the menu's language; the notes are already in it)
				AsNamed named(*this);
				text(l, false, SIZE, X0, y0 + i * PITCH, at >= r.notes ? p.muted : p.ink);
			}
		};
		// how far: the end of the bottom panel's last line, of all of them
		int shown = std::min(n, r.top + VIS);
		std::string where = n <= VIS ? "" : std::to_string(n ? shown * 100 / n : 100) + "%";
		be->beginScreen(0);
		box("screen", false, 0, 0, 640, 480);
		box("hero", false, 18, 20, 604, 436);
		float ww = where.empty() ? 0 : text(where, true, 17, 0, 0, p.muted, false, 0, true);
		{
			AsNamed named(*this);
			text(fit(r.name, true, 20, WIDTH - ww - 16), true, 20, X0, 36, p.ink);
		}
		if (!where.empty()) text(where, true, 17, X0 + WIDTH - ww, 39, p.muted);
		be->fill(std::round(X(X0)), std::round(X(66)), std::round(X(WIDTH)), std::max(1.0f, std::round(X(2))), p.line);
		lines(r.top, TOP_Y);
		be->endScreen();

		be->beginScreen(1);
		box("screen", false, 0, 0, 640, 480);
		box("hero", false, 18, 14, 604, 394);
		lines(r.top + PER, BOT_Y);
		drawChips(textChips());
		be->endScreen();
	}
}
