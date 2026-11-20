// banana-ui's achievements list (src/achievements.*) without the page: the systems' consoles, the server's replies
// parsed, the card's cache written and read back, the ROM's hash (a .zip's too), and the loader's states against
// made-up replies (BANANA_RA_FIXTURE's way, through Job::post). tests/run.sh runs it: tests/ach-test <scratch dir>
#include "achievements.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <zlib.h>

using namespace banana;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); fails++; } } while (0)

static const char* PATCH = R"({"Success":true,"PatchData":{"ID":1234,"Title":"Peel Quest","ConsoleID":5,"ImageIcon":"/Images/1.png",
"Achievements":[
 {"ID":11,"MemAddr":"0xH0001=1_\"quoted\"","Title":"First steps","Description":"Leave the house","Points":5,"Author":"a","BadgeName":"10011","Flags":3},
 {"ID":12,"MemAddr":"","Title":"Unofficial","Description":"not core","Points":10,"BadgeName":"10012","Flags":5},
 {"ID":101000001,"MemAddr":"","Title":"Warning: Unknown Emulator","Description":"","Points":0,"BadgeName":"00000","Flags":3},
 {"ID":13,"MemAddr":"","Title":"Café \"time\"","Description":"Line one\nline two\twith a tab","Points":25,"BadgeName":"10013","Flags":3},
 {"ID":14,"MemAddr":"","Title":"Last","Description":"","Points":0,"BadgeName":"10014","Flags":3}
],"Leaderboards":[]}})";

static void write(const std::string& path, const std::string& body) { std::ofstream(path, std::ios::binary) << body; }

// a .zip with one stored member (what ZipDir reads): local header, data, central directory, end record
static void storedZip(const std::string& path, const std::string& name, const std::string& data)
{
	uint32_t crc = (uint32_t)crc32(0, (const Bytef*)data.data(), (uInt)data.size());
	std::string z;
	auto u16 = [&](uint32_t v) { z += (char)(v & 255); z += (char)(v >> 8 & 255); };
	auto u32 = [&](uint32_t v) { u16(v & 0xffff); u16(v >> 16); };
	u32(0x04034b50); u16(10); u16(0); u16(0); u16(0); u16(0x21); u32(crc); u32((uint32_t)data.size()); u32((uint32_t)data.size());
	u16((uint32_t)name.size()); u16(0); z += name; z += data;
	uint32_t cd = (uint32_t)z.size();
	u32(0x02014b50); u16(20); u16(10); u16(0); u16(0); u16(0); u16(0x21); u32(crc); u32((uint32_t)data.size()); u32((uint32_t)data.size());
	u16((uint32_t)name.size()); u16(0); u16(0); u16(0); u16(0); u32(0); u32(0); z += name;
	uint32_t cdLen = (uint32_t)z.size() - cd;
	u32(0x06054b50); u16(0); u16(0); u16(1); u16(1); u32(cdLen); u32(cd); u16(0);
	write(path, z);
}

static ach::Loader::Snapshot wait(const std::shared_ptr<ach::Loader>& l)
{
	for (int i = 0; i < 500 && l->busy(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(10));
	return l->snapshot();
}

int main(int argc, char** argv)
{
	std::string dir = argc > 1 ? argv[1] : "/tmp";
	dir += "/ach-test";
	if (system(("rm -rf '" + dir + "' && mkdir -p '" + dir + "/cache'").c_str()) != 0) return 1;
	const std::string cache = dir + "/cache";

	// ---- the systems: every one RetroAchievements has (and rhash can hash), none for the rest
	struct { const char* id; uint32_t c; } sys[] = { { "nds", 18 }, { "gba", 5 }, { "gb", 4 }, { "gbc", 6 }, { "nes", 7 }, { "snes", 3 },
		{ "megadrive", 1 }, { "psx", 12 }, { "n64", 2 }, { "pico-8", 0 }, { "gameandwatch", 0 }, { "files", 0 }, { "bshare", 0 },
		{ "dodakvarter", 0 }, { "ports", 0 }, { "", 0 }, { "NDS", 0 }, { "mastersystem", 11 }, { "gamegear", 15 }, { "pcengine", 8 } };
	for (auto& s : sys) CHECK(ach::consoleFor(s.id) == s.c, "consoleFor(%s) = %u, wanted %u", s.id, ach::consoleFor(s.id), s.c);

	// ---- the server's replies
	ach::List l;
	CHECK(!ach::parsePatch("", l), "an empty reply parsed");
	CHECK(!ach::parsePatch(R"({"Success":false,"Error":"Unknown game"})", l), "a failed reply parsed");
	CHECK(ach::parsePatch(PATCH, l), "the patch didn't parse");
	CHECK(l.gameId == 1234 && l.title == "Peel Quest", "game %u '%s'", l.gameId, l.title.c_str());
	CHECK(l.items.size() == 3, "%zu core achievements, wanted 3 (no unofficial, no warning)", l.items.size());
	if (l.items.size() == 3)
	{
		CHECK(l.items[0].id == 11 && l.items[1].id == 13 && l.items[2].id == 14, "not in the server's order");
		CHECK(l.items[1].title == "Caf\xc3\xa9 \"time\"", "title '%s'", l.items[1].title.c_str());
		CHECK(l.items[1].desc == "Line one\nline two\twith a tab", "desc '%s'", l.items[1].desc.c_str());
		CHECK(l.items[1].points == 25 && l.items[1].badge == "10013", "points %u badge %s", l.items[1].points, l.items[1].badge.c_str());
	}
	CHECK(l.points() == 30, "points %u", l.points());
	CHECK(ach::parseUnlocks(R"({"Success":true,"UserUnlocks":[11,13,999],"GameID":1234,"HardcoreMode":false})", false, l), "softcore unlocks");
	CHECK(ach::parseUnlocks(R"({"Success":true,"UserUnlocks":[13],"GameID":1234,"HardcoreMode":true})", true, l), "hardcore unlocks");
	CHECK(!ach::parseUnlocks(R"({"Success":false})", true, l), "a failed unlocks reply parsed");
	CHECK(l.unlocked() == 2 && l.pointsEarned() == 30, "%d unlocked, %u points", l.unlocked(), l.pointsEarned());
	CHECK(l.items[0].soft == ach::UNKNOWN && l.items[0].hard == 0, "soft %lld hard %lld", l.items[0].soft, l.items[0].hard);
	CHECK(l.items[1].hard == ach::UNKNOWN, "hardcore not marked");
	CHECK(!l.items[2].unlocked(), "a locked one unlocked");
	// the times known from before (the game panel's session) stay
	ach::List old = l;
	old.items[1].hard = 1700000000; old.items[0].soft = 1690000000;
	ach::keepTimes(l, old);
	CHECK(l.items[1].hard == 1700000000 && l.items[0].soft == 1690000000, "times not kept: %lld %lld", l.items[1].hard, l.items[0].soft);
	CHECK(l.items[1].when() == 1700000000, "when %lld", l.items[1].when());

	// ---- the cache: ids and lists written and read back
	write(dir + "/rom.gb", std::string(1000, 'x'));
	ach::RomKey k = ach::romKey(dir + "/rom.gb");
	CHECK(k.size == 1000 && k.mtime > 0, "romKey %lld %lld", k.size, k.mtime);
	std::string h; uint32_t id = 0;
	CHECK(!ach::readId(cache, k, h, id), "an id with nothing cached");
	CHECK(ach::writeId(cache, k, "0123456789abcdef0123456789abcdef", 1234), "writeId");
	CHECK(ach::writeId(cache, ach::RomKey{ dir + "/other\tname.gb", 5, 6 }, "ffff", 0), "writeId (a tab in the name)");
	CHECK(ach::readId(cache, k, h, id) && h == "0123456789abcdef0123456789abcdef" && id == 1234, "readId: '%s' %u", h.c_str(), id);
	CHECK(ach::readId(cache, ach::RomKey{ dir + "/other\tname.gb", 5, 6 }, h, id) && h == "ffff" && id == 0, "readId (tab): '%s' %u", h.c_str(), id);
	CHECK(ach::writeId(cache, k, "0123456789abcdef0123456789abcdef", 4321), "writeId again");
	CHECK(ach::readId(cache, k, h, id) && id == 4321, "the id written again wasn't the one read: %u", id);
	{
		std::ifstream f(cache + "/ids.tsv"); std::string all((std::istreambuf_iterator<char>(f)), {});
		size_t n = 0; for (size_t at = 0; (at = all.find(k.path, at)) != std::string::npos; at++) n++;
		CHECK(n == 1, "the ROM is %zu times in ids.tsv", n);
	}
	ach::RomKey changed = k; changed.size = 999;
	CHECK(!ach::readId(cache, changed, h, id), "a changed file kept its old id");
	l.fetched = 1762900000;
	CHECK(ach::writeList(cache, l), "writeList");
	ach::List back;
	CHECK(ach::readList(cache, 1234, back), "readList");
	CHECK(back.gameId == 1234 && back.title == l.title && back.fetched == l.fetched && back.items.size() == l.items.size(), "the list read back differs");
	for (size_t i = 0; i < back.items.size() && i < l.items.size(); i++)
	{
		const auto &a = back.items[i], &b = l.items[i];
		CHECK(a.id == b.id && a.title == b.title && a.desc == b.desc && a.badge == b.badge && a.points == b.points && a.soft == b.soft && a.hard == b.hard,
			"item %zu read back differs: '%s' '%s'", i, a.title.c_str(), a.desc.c_str());
	}
	CHECK(!ach::readList(cache, 99, back), "a list never written was read");
	write(cache + "/game-77.tsv", "garbage\n\t\t\n");
	CHECK(!ach::readList(cache, 77, back), "a broken file was read as a list");
	CHECK(access((cache + "/game-1234.tsv.part").c_str(), F_OK) != 0, "a .part left behind");

	// ---- the ROM's hash: a Game Boy ROM is hashed whole (rcheevos), a .zip's game as its contents
	std::string rom;
	for (int i = 0; i < 100; i++) rom += "BananaOS!";
	write(dir + "/game.gb", rom);
	const std::string want = "b92af45516e0d37d295b9e9bf6c8a097";	// (md5sum of the same bytes)
	CHECK(ach::hashRom(dir + "/game.gb", 4, dir) == want, "game.gb hashed '%s'", ach::hashRom(dir + "/game.gb", 4, dir).c_str());
	storedZip(dir + "/game.zip", "Game (Europe).gb", rom);
	CHECK(ach::hashRom(dir + "/game.zip", 4, dir) == want, "game.zip hashed '%s'", ach::hashRom(dir + "/game.zip", 4, dir).c_str());
	CHECK(ach::hashRom(dir + "/nothing.gb", 4, dir).empty(), "a missing file hashed");
	write(dir + "/game.7z", rom);	// (a .7z isn't unpacked here: no hash, rather than the archive's bytes as the game's)
	CHECK(ach::hashRom(dir + "/game.7z", 4, dir).empty(), "a .7z hashed as it is");
	CHECK(ach::hashRom(dir + "/game.gb", 0, dir).empty(), "hashed for no console");

	// ---- the loader against made-up replies: no request starts a session
	std::string asked;
	auto fake = [&](bool online, bool known)
	{
		return [&asked, online, known](const std::string& f) -> std::string
		{
			std::string r = f.substr(2, f.find('&') - 2);
			asked += r + (f.find("&h=1") != std::string::npos ? "-h1" : "") + " ";
			if (!online) return "";
			if (r == "gameid") return known ? R"({"Success":true,"GameID":1234})" : R"({"Success":true,"GameID":0})";
			if (r == "login2") return f.find("p=right") != std::string::npos ? R"({"Success":true,"User":"good","Token":"tok"})" : R"({"Success":false,"Error":"Invalid user/password combination."})";
			if (r == "patch") return PATCH;
			if (r == "unlocks") return f.find("&h=1") != std::string::npos ? R"({"Success":true,"UserUnlocks":[13]})" : R"({"Success":true,"UserUnlocks":[11]})";
			return "";
		};
	};
	auto job = [&](const std::string& romPath, const std::string& system) { ach::Loader::Job j; j.rom = romPath; j.system = system; j.cacheDir = dir + "/lc"; j.badgeDir = dir + "/badges"; j.account = { "good", "tok", "" }; return j; };
	{
		auto j = job(dir + "/game.gb", "gb"); j.account = {};
		auto s = wait(ach::Loader::start(j));
		CHECK(s.state == ach::Loader::NO_ACCOUNT, "no account: %s", ach::stateName(s.state));
	}
	{
		auto s = wait(ach::Loader::start(job(dir + "/game.gb", "pico-8")));
		CHECK(s.state == ach::Loader::NO_SYSTEM, "PICO-8: %s", ach::stateName(s.state));
	}
	{
		auto j = job(dir + "/game.gb", "gb"); j.post = fake(false, true);
		auto s = wait(ach::Loader::start(j));
		CHECK(s.state == ach::Loader::NO_CONNECTION, "offline, nothing cached: %s", ach::stateName(s.state));
	}
	{
		auto j = job(dir + "/game.gb", "gb"); j.post = fake(true, false);
		auto s = wait(ach::Loader::start(j));
		CHECK(s.state == ach::Loader::NO_GAME, "a game the server doesn't know: %s", ach::stateName(s.state));
	}
	{
		auto j = job(dir + "/missing.gb", "gb"); j.post = fake(true, true);
		auto s = wait(ach::Loader::start(j));
		CHECK(s.state == ach::Loader::NO_FILE, "a missing ROM: %s", ach::stateName(s.state));
	}
	asked.clear();
	{
		auto j = job(dir + "/game.gb", "gb"); j.post = fake(true, true);
		auto s = wait(ach::Loader::start(j));
		CHECK(s.state == ach::Loader::READY && !s.offline, "online: %s%s", ach::stateName(s.state), s.offline ? " (offline)" : "");
		CHECK(s.list.items.size() == 3 && s.list.unlocked() == 2, "online: %zu items, %d unlocked", s.list.items.size(), s.list.unlocked());
		CHECK(asked == "gameid patch unlocks unlocks-h1 ", "asked '%s'", asked.c_str());
		std::string hh; uint32_t ii = 0;
		CHECK(ach::readId(dir + "/lc", ach::romKey(dir + "/game.gb"), hh, ii) && hh == want && ii == 1234, "the id wasn't cached: '%s' %u", hh.c_str(), ii);
	}
	asked.clear();
	{
		// identified before: no hashing, no gameid; the server down: the cached list, offline
		auto j = job(dir + "/game.gb", "gb"); j.post = fake(false, true);
		auto s = wait(ach::Loader::start(j));
		CHECK(s.state == ach::Loader::READY && s.offline && s.list.items.size() == 3 && s.list.fetched > 0, "offline from the cache: %s%s, %zu",
			ach::stateName(s.state), s.offline ? " (offline)" : "", s.list.items.size());
		CHECK(asked.find("gameid") == std::string::npos, "asked for the game id again: '%s'", asked.c_str());
	}
	{
		// Wi-Fi off: the cache alone, nothing asked
		asked.clear();
		auto j = job(dir + "/game.gb", "gb"); j.post = fake(true, true); j.network = false;
		auto s = wait(ach::Loader::start(j));
		CHECK(s.state == ach::Loader::READY && s.offline && asked.empty(), "Wi-Fi off: %s, asked '%s'", ach::stateName(s.state), asked.c_str());
	}
	{
		// a password and no token: logged in first (r=login2, read-only), then the rest; a wrong one says so
		asked.clear();
		auto j = job(dir + "/game.gb", "gb"); j.post = fake(true, true); j.account = { "good", "", "right" };
		auto s = wait(ach::Loader::start(j));
		CHECK(s.state == ach::Loader::READY && !s.offline && asked == "login2 patch unlocks unlocks-h1 ", "password: %s, asked '%s'", ach::stateName(s.state), asked.c_str());
		j.account = { "good", "", "wrong" };
		s = wait(ach::Loader::start(j));
		CHECK(s.state == ach::Loader::LOGIN_FAILED, "a wrong password: %s", ach::stateName(s.state));
	}
	CHECK(asked.find("startsession") == std::string::npos, "a session was started");
	if (!fails) fprintf(stderr, "ach-test: all passed\n");
	return fails ? 1 : 0;
}
