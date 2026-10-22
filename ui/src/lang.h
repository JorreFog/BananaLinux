#pragma once
// The menu's language: the translations in /usr/share/banana/lang/<code>.tsv (tools/lang-from-docx.py makes them from a
// translator's document). Every text the menu lays out is looked up as it is (rnds' translator hook), so the code keeps
// its English strings; a line with a placeholder ({n}, %d, %s...) matches the text the code filled in, and the
// translation gets the same values. Lines with no translation stay English (and are listed, see missingLog).
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

	// the texts shown with no translation, one a line (for the translator's next round)
	void missingLog(const std::string& path);
}
