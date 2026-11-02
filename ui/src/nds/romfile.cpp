// banana-ui: where the DS Pokémon companion finds the game's cartridge image (romfile.h).
#include "nds/romfile.h"
#include "zip.h"
#include <cstring>
#include <fstream>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace banana::nds
{
	static bool endsWith(const std::string& s, const char* e)
	{
		const size_t n = strlen(e);
		return s.size() > n && !strcasecmp(s.c_str() + s.size() - n, e);
	}

	bool romZipped(const std::string& path) { return endsWith(path, ".zip"); }

	bool romHeader(const std::string& path, uint8_t hdr[0x200])
	{
		if (!romZipped(path))
		{
			std::ifstream f(path, std::ios::binary);
			return f.read((char*)hdr, 0x200) && f.gcount() == 0x200;
		}
		// the first DS game in it (run-bananads takes the first too: a DSi game's .dsi or .cia as well), streamed only
		// as far as the header (a tester's Platinum .zip showed no companion: the zip's own bytes were read as a header)
		std::vector<uint8_t> h;
		readZip(path, [](const std::vector<rnds::ZipMember>& ms)
		{
			for (size_t i = 0; i < ms.size(); i++)
				if (endsWith(ms[i].name, ".nds") || endsWith(ms[i].name, ".dsi") || endsWith(ms[i].name, ".cia")) return (int)i;
			return -1;
		}, [&](const uint8_t* p, size_t n)
		{
			h.insert(h.end(), p, p + std::min(n, 0x200 - h.size()));
			return h.size() < 0x200;
		});
		if (h.size() != 0x200) return false;
		memcpy(hdr, h.data(), 0x200);
		return true;
	}

	std::string unzippedPath(const std::string& zip, const std::string& unzipDir)
	{
		// (run-bananads: N=${ROM##*/}; N=$C/${N%.*}.nds)
		std::string base = zip.substr(zip.find_last_of('/') + 1);
		base = base.substr(0, base.find_last_of('.'));
		return unzipDir + "/" + base + ".nds";
	}

	std::string waitUnzipped(const std::string& zip, const std::string& unzipDir, const uint8_t hdr[0x200],
		const std::atomic<bool>& stop, int timeoutMs)
	{
		const std::string path = unzippedPath(zip, unzipDir), busy = unzipDir + "/.unzip";
		for (int waited = 0; ; waited += 100)
		{
			struct stat ns {}, zs {}, us {};
			// (the copy's time is when run-bananads unpacked it - busybox's unzip gives it the time of unpacking, checked on
			// the Plus - so a copy older than the zip is about to be unpacked again: whole seconds, as busybox's test -nt.
			// The header check covers a zip renamed onto another game's name)
			if (stat(path.c_str(), &ns) == 0 && ns.st_size > 0x200 && stat(busy.c_str(), &us) != 0 &&
				stat(zip.c_str(), &zs) == 0 && zs.st_mtime <= ns.st_mtime)
			{
				uint8_t h[0x200];
				if (romHeader(path, h) && !memcmp(h, hdr, 0x200)) return path;
			}
			if (stop || waited >= timeoutMs) return "";
			usleep(100000);
		}
	}
}
