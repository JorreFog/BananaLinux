// banana-ui's translations (src/lang.cpp) on a made-up language: a line with a number in it (%d, %ld...) takes
// numbers only, so a name that merely starts like it ("Slot Machine") stays as it is (tests/run.sh runs it)
#include "lang.h"
#include <cstdio>
#include <fstream>
#include <string>

using namespace banana;

static int fails = 0;
static void same(const char* what, const std::string& got, const std::string& want)
{
	if (got != want) { fprintf(stderr, "FAIL %s: \"%s\", wanted \"%s\"\n", what, got.c_str(), want.c_str()); fails++; }
}

int main(int argc, char** argv)
{
	std::string dir = argc > 1 ? argv[1] : "/tmp";
	{
		std::ofstream f(dir + "/xx.tsv");
		f << "#: language = Test\n"
		  << "menu\t\tSlot %d\tPaikka %d\n"
		  << "menu\t\t%d games\t%d peliä\n"
		  << "menu\t\t%ldh %02ldm\t%ldt %02ldmin\n"
		  << "menu\t\t%.1f MB\t%.1f Mt\n"
		  << "menu\t\tKeep %s\tSäilytä %s\n"
		  << "menu\t\tHello {name}\tHei {name}\n";
	}
	if (!lang::load(dir, "xx", { "menu" })) { fprintf(stderr, "FAIL the test language didn't load\n"); return 1; }
	same("a number", lang::tr("Slot 3"), "Paikka 3");
	same("a negative number", lang::tr("Slot -2"), "Paikka -2");
	same("a name that isn't a number", lang::tr("Slot Machine"), "Slot Machine");
	same("a number and more", lang::tr("Slot 2.sav"), "Slot 2.sav");
	same("a number first", lang::tr("12 games"), "12 peliä");
	same("words where the number goes", lang::tr("Many games"), "Many games");
	same("two numbers", lang::tr("3h 05m"), "3t 05min");
	same("a decimal", lang::tr("1.5 MB"), "1.5 Mt");
	same("not a decimal", lang::tr("big MB"), "big MB");
	same("text where text goes", lang::tr("Keep both"), "Säilytä both");
	same("a named slot", lang::tr("Hello Joar"), "Hei Joar");
	if (!fails) fprintf(stderr, "lang-raw-test: all passed\n");
	return fails ? 1 : 0;
}
