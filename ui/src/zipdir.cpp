#include "zipdir.h"
#include <algorithm>
#include <cstring>
#include <sys/stat.h>
#include <zlib.h>

namespace banana
{
	static uint32_t le16(const uint8_t* p) { return p[0] | (p[1] << 8); }
	static uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
	static uint64_t le64(const uint8_t* p) { return le32(p) | ((uint64_t)le32(p + 4) << 32); }

	bool ZipEntry::dir() const
	{
		if (!name.empty() && (name.back() == '/' || name.back() == '\\')) return true;
		if ((madeBy >> 8) == 3) return ((attrs >> 16) & 0170000) == 0040000;	// made on Unix: its mode
		return attrs & 0x10;													// DOS, NTFS...: FILE_ATTRIBUTE_DIRECTORY
	}

	bool ZipEntry::symlink() const
	{
		return (madeBy >> 8) == 3 && ((attrs >> 16) & 0170000) == 0120000;
	}

	time_t ZipEntry::mtime() const
	{
		if (!dosDate) return 0;
		struct tm t = {};
		t.tm_year = 80 + (dosDate >> 9);
		t.tm_mon = ((dosDate >> 5) & 15) - 1;
		t.tm_mday = dosDate & 31;
		t.tm_hour = dosTime >> 11;
		t.tm_min = (dosTime >> 5) & 63;
		t.tm_sec = (dosTime & 31) * 2;
		t.tm_isdst = -1;
		return mktime(&t);
	}

	bool ZipDir::at(uint64_t pos) { return fseeko(f, (off_t)pos, SEEK_SET) == 0; }
	bool ZipDir::get(void* p, size_t n) { return fread(p, 1, n, f) == n; }

	bool ZipDir::open(const std::string& path)
	{
		f = fopen(path.c_str(), "rb");
		if (!f || fseeko(f, 0, SEEK_END) != 0) return false;
		total = (uint64_t)ftello(f);
		// the end of central directory record: 22 bytes, then a comment of up to 64 KB
		size_t tail = (size_t)std::min<uint64_t>(total, 22 + 65535);
		std::vector<uint8_t> t(tail);
		if (tail < 22 || !at(total - tail) || !get(t.data(), tail)) return false;
		long eocd = -1;
		for (long i = (long)tail - 22; i >= 0; i--)
			if (le32(&t[i]) == 0x06054b50) { eocd = i; break; }
		if (eocd < 0) return false;
		uint64_t count = le16(&t[eocd + 10]), cdSize = le32(&t[eocd + 12]), cdAt = le32(&t[eocd + 16]);
		if (count == 0xffff || cdSize == 0xffffffffu || cdAt == 0xffffffffu)
		{
			// zip64: the locator just before the end record says where the zip64 end record is
			uint64_t end = total - tail + (uint64_t)eocd;
			uint8_t loc[20], r[56];
			if (end < 20 || !at(end - 20) || !get(loc, 20) || le32(loc) != 0x07064b50) return false;
			if (!at(le64(loc + 8)) || !get(r, 56) || le32(r) != 0x06064b50) return false;
			count = le64(r + 32); cdSize = le64(r + 40); cdAt = le64(r + 48);
		}
		if (cdAt + cdSize > total || cdSize > (1u << 30)) return false;
		std::vector<uint8_t> cd((size_t)cdSize);
		if (cdSize && (!at(cdAt) || !get(cd.data(), cd.size()))) return false;
		size_t p = 0;
		for (uint64_t i = 0; i < count; i++)
		{
			if (p + 46 > cd.size() || le32(&cd[p]) != 0x02014b50) return false;
			const uint8_t* e = &cd[p];
			ZipEntry z;
			z.madeBy = le16(e + 4); z.flags = le16(e + 8); z.method = le16(e + 10);
			z.dosTime = le16(e + 12); z.dosDate = le16(e + 14); z.crc = le32(e + 16);
			z.csize = le32(e + 20); z.size = le32(e + 24); z.attrs = le32(e + 38); z.local = le32(e + 42);
			size_t nl = le16(e + 28), xl = le16(e + 30), cl = le16(e + 32);
			if (p + 46 + nl + xl + cl > cd.size()) return false;
			z.name.assign((const char*)e + 46, nl);
			z.extra.assign((const char*)e + 46 + nl, xl);
			// zip64: the sizes and the offset that didn't fit are in extra field 1, in this order
			if (z.size == 0xffffffffu || z.csize == 0xffffffffu || z.local == 0xffffffffu)
			{
				const uint8_t* x = (const uint8_t*)z.extra.data();
				for (size_t q = 0; q + 4 <= xl;)
				{
					size_t tag = le16(x + q), sz = le16(x + q + 2), o = q + 4;
					if (q + 4 + sz > xl) break;
					if (tag == 1)
					{
						if (z.size == 0xffffffffu && o + 8 <= q + 4 + sz) { z.size = le64(x + o); o += 8; }
						if (z.csize == 0xffffffffu && o + 8 <= q + 4 + sz) { z.csize = le64(x + o); o += 8; }
						if (z.local == 0xffffffffu && o + 8 <= q + 4 + sz) z.local = le64(x + o);
						break;
					}
					q += 4 + sz;
				}
			}
			list.push_back(z);
			p += 46 + nl + xl + cl;
		}
		return true;
	}

	bool ZipDir::read(const ZipEntry& e, const std::function<bool(const uint8_t*, size_t)>& sink)
	{
		if (!f || e.encrypted() || (e.method != 0 && e.method != 8)) return false;
		// the data is after the member's local header, whose name and extra field may differ from the directory's
		uint8_t lh[30];
		if (!at(e.local) || !get(lh, 30) || le32(lh) != 0x04034b50) return false;
		uint64_t dataAt = e.local + 30 + le16(lh + 26) + le16(lh + 28);
		if (dataAt + e.csize > total || !at(dataAt)) return false;

		std::vector<uint8_t> in(256 * 1024);
		uLong crc = crc32(0, nullptr, 0);
		uint64_t left = e.csize, out = 0;
		if (e.method == 0)
		{
			while (left > 0)
			{
				size_t n = (size_t)std::min<uint64_t>(left, in.size());
				if (!get(in.data(), n)) return false;
				left -= n;
				crc = crc32(crc, in.data(), (uInt)n);
				if (!sink(in.data(), n)) return true;
			}
			return crc == e.crc;
		}
		z_stream s;
		memset(&s, 0, sizeof s);
		if (inflateInit2(&s, -MAX_WBITS) != Z_OK) return false;	// raw deflate
		std::vector<uint8_t> buf(256 * 1024);
		bool ok = true;
		int r = Z_OK;
		while (r != Z_STREAM_END)
		{
			if (s.avail_in == 0)
			{
				if (left == 0) { ok = false; break; }	// cut short
				size_t n = (size_t)std::min<uint64_t>(left, in.size());
				if (!get(in.data(), n)) { ok = false; break; }
				left -= n;
				s.next_in = in.data();
				s.avail_in = (uInt)n;
			}
			s.next_out = buf.data();
			s.avail_out = (uInt)buf.size();
			r = inflate(&s, Z_NO_FLUSH);
			if (r != Z_OK && r != Z_STREAM_END) { ok = false; break; }
			size_t got = buf.size() - s.avail_out;
			if (!got) continue;
			out += got;
			crc = crc32(crc, buf.data(), (uInt)got);
			if (!sink(buf.data(), got)) { inflateEnd(&s); return true; }
		}
		inflateEnd(&s);
		return ok && crc == e.crc && out == e.size;
	}
}
