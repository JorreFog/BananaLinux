// Fuzz target: a layout theme (layout.cpp), the whole menu drawn from a theme's JSON. Each input is written as the
// theme file, and a menu made from it is used as on the handheld, over a small library: home, a move, the library, a
// move and a tap, the launch view, back; the clock moves on between them (the eased scroll, the animations), and the
// frames are drawn. One input in four at the RG DS Plus's 1024x768, the rest at 640x480.
#include "../src/layout.h"
#include "../src/library.h"
#include "../src/soft.h"
#include "../third_party/stb_image.h"
#include "../third_party/stb_image_write.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace banana;

static std::string root, assets, interFonts;
static Library* lib;

static void put(const std::string& path, const std::string& s)
{
	if (FILE* f = fopen(path.c_str(), "wb")) { fwrite(s.data(), 1, s.size(), f); fclose(f); }
}

// the pictures a theme names (box art, its own images), as banana-ui decodes them
static rnds::Bitmap decode(const std::string& path)
{
	rnds::Bitmap b;
	int w, h, n;
	uint8_t* p = stbi_load(path.c_str(), &w, &h, &n, 4);
	if (!p) return b;
	b.w = w; b.h = h;
	b.px.assign(p, p + (size_t)w * h * 4);
	stbi_image_free(p);
	return b;
}

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
	const char* a = getenv("BANANA_FUZZ_ASSETS");
	assets = a ? a : "ui/assets/rnds";
	interFonts = assets + "/../banana/fonts";
	const char* conf = getenv("BANANA_FUZZ_SYSTEMS");
	root = "/tmp/banana-fuzz-layout-" + std::to_string(getpid());
	for (const char* d : { "", "/roms", "/roms/nds", "/roms/nds/media", "/roms/nds/media/box", "/roms/gba", "/data" }) mkdir((root + d).c_str(), 0755);
	for (const char* g : { "/roms/nds/Peel Quest.nds", "/roms/nds/Banana Kart DS.nds", "/roms/nds/Yellow Planet [!].nds", "/roms/gba/Orchard.gba" }) put(root + g, "x");
	uint8_t px[16 * 12 * 3];
	for (size_t i = 0; i < sizeof px; i++) px[i] = (uint8_t)(i * 37);
	stbi_write_png((root + "/roms/nds/media/box/Peel Quest.png").c_str(), 16, 12, 3, px, 16 * 3);
	put(root + "/data/playstats.tsv", "nds/Peel Quest.nds\t12\t3600\t1700000000\n");
	rnds::setDecoder(decode);
	lib = new Library(conf ? conf : "system/rootfs/etc/banana/systems.conf", root + "/roms", root + "/data", assets);
	lib->scan();
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	static unsigned long n = 0;
	const std::string path = root + "/theme.json";
	put(path, std::string((const char*)data, size));
	if (!isLayoutTheme(path)) return 0;
	const bool plus = n++ % 4 == 3;
	SoftBackend be(plus ? 1024 : 640, plus ? 768 : 480);
	LayoutMenu ui(lib, &be, path, be.height() / 480.0f, assets + "/fonts", interFonts);
	ui.setLog([](const std::string&) {});
	double now = 100000;
	auto frame = [&](double ms) { now += ms; ui.update(now); ui.finishWork(); ui.render(); };
	using B = rnds::UI::Button;
	ui.setHome(0);
	frame(0);
	ui.press(B::RIGHT); frame(150);
	ui.press(B::A); frame(600);
	ui.press(B::DOWN); frame(16);
	ui.touch(1, 160 * be.height() / 480.0f, 250 * be.height() / 480.0f, true);
	ui.touchMove(1, 200, 260);
	ui.touch(1, 200, 260, false);
	ui.press(B::A); frame(350);
	ui.press(B::B); frame(2000);
	ui.nextWake();
	ui.animating();
	return 0;
}
