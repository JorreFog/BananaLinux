#pragma once
// The menu's language: the translations in /usr/share/banana/lang/<code>.tsv (tools/lang-from-docx.py makes them from a
// translator's document). Every text the menu lays out is looked up as it is (rnds' translator hook), so the code keeps
// its English strings; a line with a placeholder ({n}, %d, %s...) matches the text the code filled in, and the
// translation gets the same values. Lines with no translation stay English (and are listed, see missingLog).
#include <ctime>
#include <string>
#include <vector>

namespace banana::lang
{
	struct Info { std::string code, name, nameEn, by; };	// "zh-cn", "简体中文", "Simplified Chinese", "Majirefy"

	// the languages there are files for (English first, always there)
	std::vector<Info> available(const std::string& dir);
	// the language to use from now on ("en" or one with no file: English); parts: the file's parts this program shows
	bool load(const std::string& dir, const std::string& code, const std::vector<std::string>& parts);
	const Info& current();
	bool active();	// a language other than English is loaded

	std::string tr(const std::string& text);
	// the translation meant for one place, when a part has the same English line twice (the file's context column)
	std::string tr(const std::string& context, const std::string& text);
	std::u32string tr32(const std::u32string& text);	// (the hook)
	// text in another language than the one loaded (its file in dir, read once): the line's translation there, else
	// text (English: text). For a question that must be readable in the language being switched to
	std::string trIn(const std::string& dir, const std::string& code, const std::string& text);

	// the date as Settings > Look & sound > Date has it (the menu's top bar): "md" 11/16, "dm" 16/11, "dm." 16.11.,
	// "iso" 2026-11-16, "mon-d" Nov 16, "d-mon" 16 Nov, "wd-mon-d" Mon, Nov 16, "wd-d-mon" Mon 16 Nov (another: "md").
	// The month's and the day's short names translated as any line is
	std::string date(const std::string& format, const struct tm& t);
	extern const char* const DATE_FORMATS[];	// the formats above, in that order (nullptr after the last)

	// the texts shown with no translation, one a line (for the translator's next round)
	void missingLog(const std::string& path);
}
