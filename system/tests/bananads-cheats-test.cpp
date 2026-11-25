// Host regression for BananaDS's shipped libretro cheat lookup.
// Link with the pinned, patched cheats.cpp and the pinned melonDS headers.
#include "cheats.h"
#include "Platform.h"

#include <cstdio>

// These tests exercise .cht lookup and parsing. There is no .mch file.
melonDS::ARCodeFile::ARCodeFile(const std::string&) : Error(true) {}
std::vector<melonDS::ARCode> melonDS::ARCodeFile::GetCodes() const noexcept { return {}; }
void melonDS::Platform::Log(LogLevel, const char*, ...) {}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string dir = argv[1];
    bananads::Cheats cheats;
    int fails = 0;
    const auto check = [&](const char* game, const char* expected) {
        cheats.load(game, dir, {dir});
        const auto wanted = *expected ? dir + "/" + expected : "";
        if (cheats.source() != wanted || cheats.count() != (*expected ? 1 : 0)) {
            std::fprintf(stderr, "%s: expected %s, got %s (%d codes)\n", game, wanted.c_str(),
                         cheats.source().c_str(), cheats.count());
            ++fails;
        }
    };
    check("Trauma Center - Under the Knife (Europe) [Undub]", "Trauma Center - Under the Knife (Europe).cht");
    check("Trauma Center - Under the Knife (USA) [Undub]", "Trauma Center - Under the Knife (USA).cht");
    check("Trauma Center - Under the Knife (Japan) [Undub]", "");
    check("Trauma Center - Under the Knife (Asia) [Undub]", "");
    check("Trauma Center - Under the Knife [Undub (Japan)] (Europe)",
          "Trauma Center - Under the Knife (Europe).cht");
    check("Trauma Center - Under the Knife (Undub)", "Trauma Center - Under the Knife (USA).cht");
    check("Trauma Center - Under the Knife", "Trauma Center - Under the Knife (USA).cht");
    check("Trauma Center - Under the Knife (Europe)", "Trauma Center - Under the Knife (Europe).cht");
    check("Dual Region (Europe) [Hack]", "Dual Region (USA, Europe).cht");
    check("Dual Region (USA, Europe) [Hack]", "Dual Region (USA, Europe).cht");
    return fails != 0;
}
