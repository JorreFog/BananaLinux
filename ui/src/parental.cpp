// banana-ui: System > Parental controls' files on the card (parental.h)
#include "parental.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <unistd.h>

namespace banana
{
	namespace parental
	{
		static std::string trim(const std::string& s)
		{
			size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
			return a == std::string::npos ? "" : s.substr(a, b - a + 1);
		}

		// the last "key = value" line's value ("" when there's none), as Settings reads its files
		static std::string value(const std::string& path, const std::string& key)
		{
			std::ifstream f(path);
			std::string ln, v;
			while (std::getline(f, ln))
			{
				size_t eq = ln.find('=');
				if (eq != std::string::npos && trim(ln.substr(0, eq)) == key) v = trim(ln.substr(eq + 1));
			}
			return v;
		}

		// written beside and renamed over: a power cut leaves the old file or the new one
		static bool writeAtomic(const std::string& path, const std::string& body)
		{
			std::string tmp = path + ".new";
			FILE* f = fopen(tmp.c_str(), "w");
			if (!f) return false;
			bool ok = fwrite(body.data(), 1, body.size(), f) == body.size();
			ok &= fflush(f) == 0 && fsync(fileno(f)) == 0;
			ok &= fclose(f) == 0;
			if (!ok || rename(tmp.c_str(), path.c_str()) != 0) { unlink(tmp.c_str()); return false; }
			return true;
		}

		// FIPS 180-4
		std::string sha256Hex(const std::string& data)
		{
			static const uint32_t K[64] = {
				0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
				0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
				0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
				0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
				0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
				0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
				0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
				0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
			uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
			std::string m = data;
			uint64_t bits = (uint64_t)data.size() * 8;
			m += (char)0x80;
			while (m.size() % 64 != 56) m += (char)0;
			for (int i = 7; i >= 0; i--) m += (char)(bits >> (i * 8));
			auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
			for (size_t c = 0; c < m.size(); c += 64)
			{
				uint32_t w[64];
				for (int i = 0; i < 16; i++)
					w[i] = (uint32_t)(uint8_t)m[c + i * 4] << 24 | (uint32_t)(uint8_t)m[c + i * 4 + 1] << 16 |
						(uint32_t)(uint8_t)m[c + i * 4 + 2] << 8 | (uint32_t)(uint8_t)m[c + i * 4 + 3];
				for (int i = 16; i < 64; i++)
				{
					uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
					uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
					w[i] = w[i - 16] + s0 + w[i - 7] + s1;
				}
				uint32_t a = h[0], b = h[1], cc = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
				for (int i = 0; i < 64; i++)
				{
					uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
					uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc));
					hh = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
				}
				h[0] += a; h[1] += b; h[2] += cc; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
			}
			char out[65];
			for (int i = 0; i < 8; i++) snprintf(out + i * 8, 9, "%08x", h[i]);
			return std::string(out, 64);
		}

		bool hasPin(const std::string& dataDir)
		{
			return value(dataDir + "/parental.conf", "pin").size() == 64;
		}

		bool on(const std::string& dataDir)
		{
			return value(dataDir + "/settings.conf", "parental") == "on" && hasPin(dataDir);
		}

		bool checkPin(const std::string& dataDir, const std::string& pin)
		{
			std::string p = dataDir + "/parental.conf", want = value(p, "pin");
			return want.size() == 64 && sha256Hex(value(p, "salt") + pin) == want;
		}

		bool setPin(const std::string& dataDir, const std::string& pin)
		{
			unsigned char r[16] = {};
			int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
			bool got = fd >= 0 && read(fd, r, sizeof r) == (ssize_t)sizeof r;
			if (fd >= 0) close(fd);
			if (!got) return false;
			char salt[33];
			for (int i = 0; i < 16; i++) snprintf(salt + i * 2, 3, "%02x", r[i]);
			return writeAtomic(dataDir + "/parental.conf",
				"# Parental controls' PIN (System > Parental controls), never kept as it is typed\n"
				"salt = " + std::string(salt) + "\npin = " + sha256Hex(salt + pin) + "\n");
		}

		long playLeft(const std::string& dataDir)
		{
			int limit = atoi(value(dataDir + "/settings.conf", "parental_minutes").c_str());
			if (limit <= 0 || !on(dataDir)) return -1;
			std::ifstream f(dataDir + "/playtime.txt");
			std::string day;
			double used = 0;
			f >> day >> used;
			char today[16];
			time_t now = time(nullptr);
			struct tm tm;
			localtime_r(&now, &tm);
			strftime(today, sizeof today, "%Y-%m-%d", &tm);
			if (day != today) used = 0;
			long left = (long)(limit * 60 - used);
			return left > 0 ? left : 0;
		}

		std::set<std::string> games(const std::string& dataDir)
		{
			std::set<std::string> s;
			std::ifstream f(dataDir + "/parental-games.txt");
			std::string ln;
			while (std::getline(f, ln)) if (!trim(ln).empty() && trim(ln)[0] != '#') s.insert(trim(ln));
			return s;
		}

		bool saveGames(const std::string& dataDir, const std::set<std::string>& keys)
		{
			std::string out;
			for (auto& k : keys) out += k + "\n";
			return writeAtomic(dataDir + "/parental-games.txt", out);
		}
	}
}
