// The menu's WAV reader on the files a theme can bring: a valid 16-bit file loads, a 44-byte file whose sample rate
// is 0 is refused (it used to divide by zero and throw from std::vector), and a truncated fmt chunk is refused.
#include "../src/sound.cpp"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace rnds { ThemeFiles themeFiles; }	// (sound.cpp's user-theme lookup; the reader is what this test calls)

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); fails++; } } while (0)

static void put16(std::vector<uint8_t>& b, uint16_t v) { b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8)); }
static void put32(std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> (8 * i))); }

// extraLen: a "LIST" chunk between fmt and data that says it is that long (0: none), holding extraBytes bytes
static std::string wav(const char* dir, const char* name, uint32_t rate, uint16_t channels, int frames, bool truncate, uint32_t extraLen = 0, int extraBytes = 0)
{
	std::vector<uint8_t> b;
	b.insert(b.end(), { 'R', 'I', 'F', 'F' }); put32(b, 36); b.insert(b.end(), { 'W', 'A', 'V', 'E' });
	b.insert(b.end(), { 'f', 'm', 't', ' ' }); put32(b, 16);
	put16(b, 1); put16(b, channels); put32(b, rate); put32(b, rate * channels * 2); put16(b, channels * 2); put16(b, 16);
	if (extraLen) { b.insert(b.end(), { 'L', 'I', 'S', 'T' }); put32(b, extraLen); for (int i = 0; i < extraBytes; i++) b.push_back('x'); }
	if (truncate) { b.resize(20); }	// the fmt header, no body
	else
	{
		b.insert(b.end(), { 'd', 'a', 't', 'a' }); put32(b, (uint32_t)frames * channels * 2);
		for (int i = 0; i < frames * channels; i++) put16(b, 0);
	}
	std::string p = std::string(dir) + "/" + name;
	FILE* f = fopen(p.c_str(), "wb");
	if (!f) { fprintf(stderr, "FAIL can't write %s\n", p.c_str()); fails++; return p; }
	fwrite(b.data(), 1, b.size(), f); fclose(f);
	return p;
}

int main(int argc, char** argv)
{
	const char* dir = argc > 1 ? argv[1] : ".";
	std::vector<int16_t> pcm;
	int rate = 0, channels = 0;

	CHECK(banana::loadWav(wav(dir, "valid.wav", 48000, 2, 2, false).c_str(), pcm, rate, channels), "a valid 48 kHz stereo file must load");
	CHECK(rate == 48000 && channels == 2, "valid file read as %d Hz, %d channels", rate, channels);

	pcm.clear(); rate = 0; channels = 0;
	CHECK(!banana::loadWav(wav(dir, "rate0.wav", 0, 1, 2, false).c_str(), pcm, rate, channels), "a rate-0 file must be refused");

	pcm.clear(); rate = 0; channels = 0;
	CHECK(!banana::loadWav(wav(dir, "truncated.wav", 48000, 2, 0, true).c_str(), pcm, rate, channels), "a truncated fmt chunk must be refused");

	// another chunk before the samples (a sound editor's LIST of its name and date): an odd length has a pad byte
	pcm.clear(); rate = 0; channels = 0;
	CHECK(banana::loadWav(wav(dir, "list.wav", 44100, 1, 3, false, 5, 6).c_str(), pcm, rate, channels) && pcm.size() == 3 && rate == 44100,
		"a file with a LIST chunk before its samples must load (%zu samples, %d Hz)", pcm.size(), rate);
	// ...and one whose length is a lie: refused, at once (0xfffffff8 + 8 wrapped to nothing and the reader never left
	// the chunk: the alarm ends this test if it does that again)
	alarm(20);
	for (uint32_t len : { 0xfffffff8u, 0xfffffff7u, 0x7fffffffu, 100u })
	{
		pcm.clear(); rate = 0; channels = 0;
		CHECK(!banana::loadWav(wav(dir, "lie.wav", 48000, 2, 2, false, len, 4).c_str(), pcm, rate, channels), "a chunk of %u bytes in a small file must be refused", len);
	}
	alarm(0);

	if (fails) return 1;
	printf("wav-test: all passed\n");
	return 0;
}
