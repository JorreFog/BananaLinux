#include "zip.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <zlib.h>

namespace banana
{
	static uint32_t le16(const uint8_t* p) { return p[0] | (p[1] << 8); }
	static uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

	struct File
	{
		FILE* f;
		explicit File(const std::string& path) : f(fopen(path.c_str(), "rb")) {}
		~File() { if (f) fclose(f); }
		bool at(uint64_t pos) { return fseeko(f, (off_t)pos, SEEK_SET) == 0; }
		bool read(void* p, size_t n) { return fread(p, 1, n, f) == n; }
	};

	struct Entry { uint32_t method = 0, flags = 0, csize = 0, local = 0; };

	bool readZip(const std::string& zip, const std::function<int(const std::vector<rnds::ZipMember>&)>& pick,
		const std::function<bool(const uint8_t*, size_t)>& sink)
	{
		File z(zip);
		if (!z.f || fseeko(z.f, 0, SEEK_END) != 0) return false;
		uint64_t size = (uint64_t)ftello(z.f);
		// the end of central directory record: 22 bytes, then a comment of up to 64 KB
		size_t tail = (size_t)std::min<uint64_t>(size, 22 + 65535);
		std::vector<uint8_t> t(tail);
		if (tail < 22 || !z.at(size - tail) || !z.read(t.data(), tail)) return false;
		long eocd = -1;
		for (long i = (long)tail - 22; i >= 0; i--)
			if (le32(&t[i]) == 0x06054b50) { eocd = i; break; }
		if (eocd < 0) return false;
		uint32_t count = le16(&t[eocd + 10]), cdSize = le32(&t[eocd + 12]), cdAt = le32(&t[eocd + 16]);
		if (count == 0xffff || cdAt == 0xffffffffu || (uint64_t)cdAt + cdSize > size) return false;	// zip64

		std::vector<uint8_t> cd(cdSize);
		if (!z.at(cdAt) || !z.read(cd.data(), cdSize)) return false;
		std::vector<rnds::ZipMember> members;
		std::vector<Entry> entries;
		size_t p = 0;
		for (uint32_t i = 0; i < count && p + 46 <= cd.size() && le32(&cd[p]) == 0x02014b50; i++)
		{
			const uint8_t* e = &cd[p];
			size_t nameLen = le16(e + 28), extraLen = le16(e + 30), commentLen = le16(e + 32);
			if (p + 46 + nameLen > cd.size()) break;
			Entry en;
			en.flags = le16(e + 8); en.method = le16(e + 10); en.csize = le32(e + 20); en.local = le32(e + 42);
			rnds::ZipMember m;
			m.name.assign((const char*)e + 46, nameLen);
			m.size = le32(e + 24);
			members.push_back(m);
			entries.push_back(en);
			p += 46 + nameLen + extraLen + commentLen;
		}
		int k = pick(members);
		if (k < 0 || k >= (int)entries.size()) return false;
		const Entry& en = entries[k];
		if ((en.flags & 1) || (en.method != 0 && en.method != 8) || en.csize == 0xffffffffu) return false;	// encrypted, zip64

		// the member's data: after its local header (whose name and extra field may differ from the directory's)
		uint8_t lh[30];
		if (!z.at(en.local) || !z.read(lh, 30) || le32(lh) != 0x04034b50) return false;
		uint64_t dataAt = (uint64_t)en.local + 30 + le16(lh + 26) + le16(lh + 28);
		if (dataAt + en.csize > size || !z.at(dataAt)) return false;

		std::vector<uint8_t> in(64 * 1024);
		uint64_t left = en.csize;
		if (en.method == 0)
		{
			while (left > 0)
			{
				size_t n = (size_t)std::min<uint64_t>(left, in.size());
				if (!z.read(in.data(), n)) return false;
				left -= n;
				if (!sink(in.data(), n)) return true;
			}
			return true;
		}
		z_stream s;
		memset(&s, 0, sizeof s);
		if (inflateInit2(&s, -MAX_WBITS) != Z_OK) return false;	// raw deflate
		std::vector<uint8_t> out(256 * 1024);
		bool ok = true, more = true;
		int r = Z_OK;
		while (more && r != Z_STREAM_END)
		{
			if (s.avail_in == 0)
			{
				if (left == 0) { ok = false; break; }	// cut short
				size_t n = (size_t)std::min<uint64_t>(left, in.size());
				if (!z.read(in.data(), n)) { ok = false; break; }
				left -= n;
				s.next_in = in.data();
				s.avail_in = (uInt)n;
			}
			s.next_out = out.data();
			s.avail_out = (uInt)out.size();
			r = inflate(&s, Z_NO_FLUSH);
			if (r != Z_OK && r != Z_STREAM_END) { ok = false; break; }
			size_t got = out.size() - s.avail_out;
			if (got) more = sink(out.data(), got);
		}
		inflateEnd(&s);
		return ok;
	}
}
