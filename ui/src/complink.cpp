// banana-ui: the companion's shared-memory link (complink.h)
#include "complink.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <time.h>
#include <unistd.h>

namespace banana
{
	namespace
	{
		double monoMs() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
	}

	bool CompLink::create(const std::string& p)
	{
		close();
		shm = bcomp_create(p.c_str());
		if (shm) path = p;
		return shm != nullptr;
	}

	void CompLink::close()
	{
		if (!shm) return;
		bcomp_unmap(shm);
		shm = nullptr;
		unlink(path.c_str());
		queued.clear();
		inFlight = false;
		nSent = 0;
	}

	bool CompLink::send(const std::string& lines)
	{
		if (!attached()) return false;
		queued += lines;
		if (!inFlight) flush();
		return true;
	}

	// the queued lines (as many as one set holds) out as one request
	bool CompLink::flush()
	{
		static const char P[] = "READ_CORE_MEMORY ";
		nSent = 0;
		uint64_t total = 0;
		size_t at = 0;
		while (at < queued.size() && nSent < BCOMP_MAX_REQ)
		{
			size_t nl = queued.find('\n', at);
			if (nl == std::string::npos) nl = queued.size();
			std::string ln = queued.substr(at, nl - at);
			if (ln.compare(0, sizeof P - 1, P) == 0)
			{
				char* e;
				unsigned long a = strtoul(ln.c_str() + sizeof P - 1, &e, 16);
				unsigned long n = strtoul(e, nullptr, 10);
				if (n > 0 && total + n <= BCOMP_DATA_SIZE)
				{
					sent[nSent++] = { (uint32_t)a, (uint32_t)n };
					total += n;
				}
				else if (n > 0) break;	// the next set
			}
			at = nl + 1;
		}
		queued.erase(0, at > queued.size() ? queued.size() : at);
		if (!nSent) return false;
		if (bcomp_request(shm, sent, nSent) != 0) { nSent = 0; return false; }
		inFlight = true;
		sentMs = monoMs();
		return true;
	}

	std::string CompLink::receive()
	{
		if (!inFlight || !shm) return "";
		uint64_t frame;
		if (!bcomp_reply(shm, &frame))
		{
			// a paused or stuck emulator: the set is given up after a second, the next goes out
			if (monoMs() - sentMs > 1000) { inFlight = false; nSent = 0; if (!queued.empty()) flush(); }
			return "";
		}
		std::string out;
		const uint8_t* d = bcomp_data(shm);
		char b[16];
		for (int i = 0; i < nSent; i++)
		{
			snprintf(b, sizeof b, "%x", sent[i].addr);
			out += "READ_CORE_MEMORY ";
			out += b;
			if (shm->status[i] == BCOMP_UNMAPPED || shm->status[i] == BCOMP_TOOBIG) out += " -1 no memory";
			else
				for (uint32_t k = 0; k < sent[i].len; k++) { snprintf(b, sizeof b, " %02x", d[k]); out += b; }
			out += '\n';
			d += sent[i].len;
		}
		inFlight = false;
		nSent = 0;
		if (!queued.empty()) flush();
		return out;
	}

	bool CompLink::ra(bcomp_ra& out) const
	{
		if (!shm) return false;
		return bcomp_ra_read(shm, &out) == 0 && out.state != BCOMP_RA_NONE;
	}
}
