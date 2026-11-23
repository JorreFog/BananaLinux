// the menu's language lookup (lang.cpp) on the shipped translations: the button names printed on the handheld
// (START, SELECT on the key caps) stay as they are, while other text the code writes in capitals still takes its
// line's translation (lang-test <dir of the .tsv files>; tests/run.sh runs it)
#include "lang.h"
#include <cstdio>
#include <string>
namespace lang = banana::lang;
static int fails;
static void expect(const std::string& got, const std::string& want, const char* what)
{
	if (got != want) { fprintf(stderr, "FAIL %s: \"%s\", not \"%s\"\n", what, got.c_str(), want.c_str()); fails++; }
}
int main(int argc, char** argv)
{
	if (argc < 2) { fprintf(stderr, "usage: lang-test <lang dir>\n"); return 2; }
	const char* langs[] = { "fi", "it", "ru", "zh-cn" };
	for (const char* code : langs)
	{
		if (!lang::load(argv[1], code, { "menu", "settings", "systems" }) || !lang::active())
		{ fprintf(stderr, "FAIL %s: not loaded\n", code); fails++; continue; }
		std::string c = code;
		// the key caps: the button's name as printed on the device (a chip "START  Settings")
		expect(lang::tr("START"), "START", (c + ": the START key cap").c_str());
		expect(lang::tr("SELECT"), "SELECT", (c + ": the SELECT key cap").c_str());
		// the Server row's help with what it says of the certificate: each case a line of its own in every language,
		// the server's name and address carried into it (put together from two sentences it matched no line at all)
		for (const std::string& en : {
			std::string("Paired with RomM 5.4.1 at https://romm.example.com. A opens its address and Check certificate. Connect there pairs again. Its certificate isn't checked (Check certificate is off)."),
			std::string("Paired with RomM 5.4.1 at https://romm.example.com. A opens its address and Check certificate. Connect there pairs again. Its certificate is one you trusted on this handheld."),
			std::string("Paired with Test server (Romhost) at https://host.example.com. A pairs again. Its certificate isn't checked (Check certificate is off)."),
			std::string("Paired with Test server (Romhost) at https://host.example.com. A pairs again. Its certificate is one you trusted on this handheld.") })
		{
			const std::string got = lang::tr(en);
			expect(got != en && got.find(".example.com") != std::string::npos ? "translated" : got, "translated", (c + ": the Server row's help with its certificate").c_str());
		}
		// the line itself still translates ("Start" on a button's label)
		expect(lang::tr("Start") == "Start" ? "untranslated" : "translated", "translated", (c + ": Start").c_str());
	}
	// a capitals line with no line of its own still takes the same line's translation in another case (4a67ccc)
	lang::load(argv[1], "fi", { "menu", "settings", "systems" });
	expect(lang::tr("RESUME"), "Jatka", "fi: RESUME (capitals) as Resume");
	expect(lang::tr("Settings"), "Asetukset", "fi: Settings");
	lang::load(argv[1], "en", {});
	expect(lang::tr("START"), "START", "en: START");
	if (!fails) fprintf(stderr, "lang-test: all passed\n");
	return fails ? 1 : 0;
}
