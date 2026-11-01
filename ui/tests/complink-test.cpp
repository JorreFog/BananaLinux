// the companion's shared-memory link (complink.h): RetroArch's READ_CORE_MEMORY lines answered through the universal
// companion export (bcomp, ~/banana-emu/common/COMPANION-SPEC.md) - a producer in this process stands in for
// BananaFront
#include "complink.h"
#include "bcomp.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main()
{
	char path[256];
	snprintf(path, sizeof path, "%s/complink-test-%d.shm", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (int)getpid());
	banana::CompLink link;
	CHECK(link.create(path));
	CHECK(!link.attached());
	CHECK(link.send("READ_CORE_MEMORY 2000010 4\n") == false);	// no producer: the caller keeps its pipe

	static uint8_t ewram[0x40000];
	for (int i = 0; i < 0x40000; i++) ewram[i] = (uint8_t)(i * 3);
	bcomp_producer p;
	CHECK(bcomp_attach(&p, path) == 0);
	bcomp_add_region(&p, "ewram", 0x02000000, 0x40000, BCOMP_R_RAM, 0x8000, ewram);
	bcomp_publish(&p);
	CHECK(link.attached());

	CHECK(link.send("READ_CORE_MEMORY 2000010 4\nREAD_CORE_MEMORY 3000000 2\n"));
	CHECK(link.receive().empty());		// not answered yet
	CHECK(link.send("READ_CORE_MEMORY 2000020 1\n"));	// queued behind the set in flight
	bcomp_service(&p, 7);
	std::string r = link.receive();
	CHECK(r == "READ_CORE_MEMORY 2000010 30 33 36 39\nREAD_CORE_MEMORY 3000000 -1 no memory\n");
	CHECK(link.receive().empty());		// the queued set goes out now
	bcomp_service(&p, 8);
	CHECK(link.receive() == "READ_CORE_MEMORY 2000020 60\n");

	// a set the producer never answers (a paused or stuck emulator) is given up after a second: the next goes out
	CHECK(link.send("READ_CORE_MEMORY 2000030 1\n"));
	CHECK(link.receive().empty() && link.waiting());
	link.ageForTest(1500);
	CHECK(link.receive().empty() && !link.waiting());
	CHECK(link.send("READ_CORE_MEMORY 2000031 1\n"));
	bcomp_service(&p, 9);
	CHECK(link.receive() == "READ_CORE_MEMORY 2000031 93\n");

	bcomp_ra_begin(&p);
	p.shm->ra.state = BCOMP_RA_ACTIVE;
	p.shm->ra.game_id = 515;
	strcpy(p.shm->ra.rp, "Lv 23, Viridian Forest");
	bcomp_ra_push(&p, BCOMP_EV_UNLOCK, 4242, "", "First Steps");
	bcomp_ra_end(&p);
	bcomp_ra ra;
	CHECK(link.ra(ra) && ra.game_id == 515 && std::string(ra.rp) == "Lv 23, Viridian Forest" && ra.ev_head == 1);

	bcomp_detach(&p);
	link.close();
	CHECK(access(path, F_OK) != 0);		// removed with the link
	if (fails) { fprintf(stderr, "%d failed\n", fails); return 1; }
	puts("complink-test: all passed");
	return 0;
}
