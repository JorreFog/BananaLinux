// the DS Pokémon companion's ROM file (nds/romfile.h): the header of a game kept in a .zip (stored and deflated, the DS
// game not the first member), and the wait for run-bananads' unpacked copy (not there, being unpacked, older than the
// zip, another game's, whole; given up when the game ends)
#include "nds/romfile.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <zlib.h>

using namespace banana::nds;
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void p16(std::vector<uint8_t>& v, uint16_t x) { v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8)); }
static void p32(std::vector<uint8_t>& v, uint32_t x) { p16(v, (uint16_t)x); p16(v, (uint16_t)(x >> 16)); }

// a .zip of the given members, each stored or deflated (raw deflate, as zip tools write it)
struct Member { std::string name; std::vector<uint8_t> data; bool deflate; };
static std::vector<uint8_t> makeZip(const std::vector<Member>& ms)
{
	std::vector<uint8_t> out, cd;
	for (auto& m : ms)
	{
		std::vector<uint8_t> body = m.data;
		if (m.deflate)
		{
			z_stream z{};
			deflateInit2(&z, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
			body.resize(deflateBound(&z, m.data.size()));
			z.next_in = (Bytef*)m.data.data(); z.avail_in = (uInt)m.data.size();
			z.next_out = body.data(); z.avail_out = (uInt)body.size();
			deflate(&z, Z_FINISH);
			body.resize(z.total_out);
			deflateEnd(&z);
		}
		const uint32_t crc = (uint32_t)crc32(0, m.data.data(), (uInt)m.data.size()), at = (uint32_t)out.size();
		const uint16_t method = m.deflate ? 8 : 0;
		p32(out, 0x04034b50); p16(out, 20); p16(out, 0); p16(out, method); p16(out, 0); p16(out, 0x21);
		p32(out, crc); p32(out, (uint32_t)body.size()); p32(out, (uint32_t)m.data.size()); p16(out, (uint16_t)m.name.size()); p16(out, 0);
		out.insert(out.end(), m.name.begin(), m.name.end());
		out.insert(out.end(), body.begin(), body.end());
		p32(cd, 0x02014b50); p16(cd, 20); p16(cd, 20); p16(cd, 0); p16(cd, method); p16(cd, 0); p16(cd, 0x21);
		p32(cd, crc); p32(cd, (uint32_t)body.size()); p32(cd, (uint32_t)m.data.size()); p16(cd, (uint16_t)m.name.size());
		p16(cd, 0); p16(cd, 0); p16(cd, 0); p16(cd, 0); p32(cd, 0); p32(cd, at);
		cd.insert(cd.end(), m.name.begin(), m.name.end());
	}
	const uint32_t cdAt = (uint32_t)out.size();
	out.insert(out.end(), cd.begin(), cd.end());
	p32(out, 0x06054b50); p16(out, 0); p16(out, 0); p16(out, (uint16_t)ms.size()); p16(out, (uint16_t)ms.size());
	p32(out, (uint32_t)cd.size()); p32(out, cdAt); p16(out, 0);
	return out;
}
static void writeFile(const std::string& p, const std::vector<uint8_t>& v) { std::ofstream(p, std::ios::binary).write((const char*)v.data(), v.size()); }
static void setTime(const std::string& p, time_t t) { struct timeval tv[2] = { { t, 0 }, { t, 0 } }; utimes(p.c_str(), tv); }

// a cartridge image: a header with a game code and title, then some bytes
static std::vector<uint8_t> rom(const char* code, size_t n = 0x4000)
{
	std::vector<uint8_t> v(n);
	for (size_t i = 0x200; i < n; i++) v[i] = (uint8_t)(i * 7);
	memcpy(v.data(), "POKEMON PL", 10);
	memcpy(v.data() + 0x0C, code, 4);
	v[0x1E] = 1;
	return v;
}

int main()
{
	char tmpl[] = "/tmp/nds-romfile-XXXXXX";
	const std::string dir = mkdtemp(tmpl), unz = dir + "/unzipped";
	mkdir(unz.c_str(), 0755);
	const auto pt = rom("CPUE"), hg = rom("IPKE");
	uint8_t h[0x200];

	// plain files, and names: a .zip by its ending in any case
	writeFile(dir + "/plain.nds", pt);
	CHECK(romHeader(dir + "/plain.nds", h) && !memcmp(h, pt.data(), 0x200));
	CHECK(!romZipped(dir + "/plain.nds") && romZipped(dir + "/a.zip") && romZipped(dir + "/A.ZIP") && !romZipped(".zip"));
	writeFile(dir + "/short.nds", std::vector<uint8_t>(0x100));
	CHECK(!romHeader(dir + "/short.nds", h));
	CHECK(!romHeader(dir + "/none.nds", h));

	// zipped: the DS game's header, not the zip's bytes; after a text member; stored and deflated; no DS game in it
	const std::string zs = dir + "/Pokemon - Platinum Version (USA) (Rev 1).zip", zd = dir + "/deflated.ZIP", zn = dir + "/none.zip";
	writeFile(zs, makeZip({ { "readme.txt", { 'h', 'i' }, false }, { "Pokemon - Platinum Version (USA) (Rev 1).nds", pt, false } }));
	writeFile(zd, makeZip({ { "game.NDS", hg, true } }));
	writeFile(zn, makeZip({ { "readme.txt", { 'h', 'i' }, true } }));
	memset(h, 0, sizeof h);
	CHECK(romHeader(zs, h) && !memcmp(h, pt.data(), 0x200));
	CHECK(romHeader(zd, h) && !memcmp(h, hg.data(), 0x200));
	CHECK(!romHeader(zn, h));

	// the unpacked copy's name (run-bananads': the zip's name without its ending)
	CHECK(unzippedPath(zs, unz) == unz + "/Pokemon - Platinum Version (USA) (Rev 1).nds");
	CHECK(unzippedPath("/x/a.b.zip", "/u") == "/u/a.b.nds");

	std::atomic<bool> stop{ false };
	const std::string copy = unzippedPath(zs, unz);
	const time_t now = time(nullptr);
	setTime(zs, now - 100);
	// not there yet: waits, then gives up
	auto t0 = std::chrono::steady_clock::now();
	CHECK(waitUnzipped(zs, unz, pt.data(), stop, 300) == "");
	CHECK(std::chrono::steady_clock::now() - t0 >= std::chrono::milliseconds(300));
	// an older copy (about to be unpacked again), another game's copy under the name, a copy being unpacked: not used
	writeFile(copy, pt); setTime(copy, now - 200);
	CHECK(waitUnzipped(zs, unz, pt.data(), stop, 0) == "");
	writeFile(copy, hg); setTime(copy, now);
	CHECK(waitUnzipped(zs, unz, pt.data(), stop, 0) == "");
	writeFile(copy, pt); setTime(copy, now);
	mkdir((unz + "/.unzip").c_str(), 0755);
	CHECK(waitUnzipped(zs, unz, pt.data(), stop, 0) == "");
	// the unpacking ends while it waits: the copy is taken
	std::thread done([&] { usleep(250000); rmdir((unz + "/.unzip").c_str()); });
	t0 = std::chrono::steady_clock::now();
	CHECK(waitUnzipped(zs, unz, pt.data(), stop, 5000) == copy);
	CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(2000));
	done.join();
	// the same second as the zip counts as not older (busybox's test -nt compares whole seconds)
	setTime(copy, now - 100);
	CHECK(waitUnzipped(zs, unz, pt.data(), stop, 0) == copy);
	// the game ended (run-bananads failed: a full card): the wait stops at once instead of holding the menu
	unlink(copy.c_str());
	std::thread end([&] { usleep(200000); stop = true; });
	t0 = std::chrono::steady_clock::now();
	CHECK(waitUnzipped(zs, unz, pt.data(), stop, 120000) == "");
	CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(1500));
	end.join();

	if (system(("rm -rf '" + dir + "'").c_str()) != 0) {}
	if (fails) { fprintf(stderr, "%d failed\n", fails); return 1; }
	fprintf(stderr, "nds-romfile-test: all passed\n");
	return 0;
}
