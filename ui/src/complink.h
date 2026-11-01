#pragma once
// banana-ui: the companion's link to an emulator through the universal companion export (bcomp.h; the spec:
// ~/banana-emu/common/COMPANION-SPEC.md). banana-ui creates /run/banana/companion.shm before the game starts and names
// it in BANANA_COMPANION; a producer (BananaFront, BananaDS) attaches to it. The Pokémon and game companions keep
// speaking RetroArch's READ_CORE_MEMORY: their request lines go into the shared memory instead of RetroArch's stdin and
// the answers come back as RetroArch's reply lines, so the same parsers serve both. RetroArch never attaches: then
// send() says no and the pipe is used as before.
#include "bcomp.h"
#include <string>

namespace banana
{
	class CompLink
	{
	public:
		~CompLink() { close(); }
		bool create(const std::string& path = BCOMP_DEFAULT_PATH);
		void close();
		bool open() const { return shm != nullptr; }
		bool attached() const { return shm && bcomp_attached(shm); }
		// READ_CORE_MEMORY lines: true when taken (sent, or queued behind the set in flight); false: no producer
		bool send(const std::string& lines);
		// the answers of the set in flight as RetroArch's reply lines, once the producer took them ("" not yet)
		std::string receive();
		bool waiting() const { return inFlight; }
		// a consistent copy of the RetroAchievements block (false: none, or the producer was writing)
		bool ra(bcomp_ra& out) const;
		const bcomp_shm* raw() const { return shm; }
		void ageForTest(double ms) { sentMs -= ms; }	// (tests: as if the set in flight were older)
	private:
		bcomp_shm* shm = nullptr;
		std::string path, queued;
		bcomp_range sent[BCOMP_MAX_REQ];
		int nSent = 0;
		bool inFlight = false;
		double sentMs = 0;				// when the set in flight went out (a set not answered in a second is given up)
		bool flush();
	};
}
