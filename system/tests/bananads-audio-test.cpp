// Run the real output loop with a scripted SPU and capture writes to ALSA's null PCM.
#include <algorithm>
#include <array>
#include <cassert>
#include "frontend.h"

namespace bananads {
struct AudioFixture {
    int frames = 2048;

    int audio_backlog() const { return frames; }
    int audio(int16_t* out, int requested) {
        const int count = std::min(frames, requested);
        for (int i = 0; i < count; ++i) {
            out[2 * i] = 12000;
            out[2 * i + 1] = -6000;
        }
        frames -= count;
        return count;
    }
    void set_audio_skew(double) {}
    Mic& mic();
};
}

#define Emu AudioFixture
#include "alsa.cpp"
#undef Emu

void melonDS::Platform::Log(LogLevel, const char*, ...) {}

static constexpr int kChunk = 256;
static constexpr int kMenuChunk = 32;
static constexpr int kResumeChunk = 34;
static constexpr int kChunks = 42;
static bananads::Alsa* output;
static int written;
static std::array<std::array<int16_t, kChunk * 2>, kChunks> samples;

extern "C" int __wrap_clock_gettime(clockid_t, timespec* now) {
    now->tv_sec = 1;
    now->tv_nsec = written * 5333333;
    return 0;
}

extern "C" int __wrap_snd_pcm_delay(snd_pcm_t*, snd_pcm_sframes_t* frames) {
    *frames = 0;
    return 0;
}

extern "C" snd_pcm_sframes_t __wrap_snd_pcm_writei(snd_pcm_t*, const void* data, snd_pcm_uframes_t frames) {
    assert(frames == kChunk);
    std::copy_n(static_cast<const int16_t*>(data), frames * 2, samples[written].begin());
    ++written;
    if (written == kMenuChunk) bananads::alsa_ui_play(output, 0);
    if (written == kResumeChunk) output->emu->frames = 4096;
    if (written == kChunks) output->run = false;
    return frames;
}

static bool run(bool servo, bool stretch) {
    bananads::AudioFixture emu;
    bananads::Alsa alsa{};
    alsa.emu = &emu;
    alsa.out_dev = "null";
    alsa.latency_us = 30000;
    alsa.servo = servo;
    alsa.stretch = stretch;
    alsa.servo_target = 768;
    alsa.first_prime = 1024;
    const int16_t menu[] = {1234, -2345};
    bananads::alsa_ui_load(&alsa, 0, menu, 1, 48000, 2);
    output = &alsa;
    written = 0;
    bananads::out_thread(&alsa);
    assert(written == kChunks);

    const auto at = [](int chunk, int frame, int channel) {
        return samples[chunk][2 * frame + channel];
    };
    const bool starts = at(2, 0, 0) == 12000 && at(2, 0, 1) == -6000;
    const bool bridges_gap = at(9, 0, 0) == 12000 && at(9, 0, 1) == -6000;
    const bool ramps_out = std::any_of(samples.begin() + 14, samples.begin() + 18, [](const auto& chunk) {
        return chunk[0] > 11900 && chunk[1] < -5950 &&
               chunk[240] > 5900 && chunk[240] < 6100 && chunk[241] > -3050 && chunk[241] < -2950 &&
               chunk[510] == 0 && chunk[511] == 0;
    });
    const bool silent = std::all_of(samples[kMenuChunk - 1].begin(), samples[kMenuChunk - 1].end(),
                                    [](int16_t v) { return v == 0; });
    const bool menu_only = at(kMenuChunk, 0, 0) == menu[0] && at(kMenuChunk, 0, 1) == menu[1] &&
                           at(kMenuChunk, 1, 0) == 0 && at(kMenuChunk, 1, 1) == 0;
    const bool resumed = at(kChunks - 1, 0, 0) == 12000 && at(kChunks - 1, 0, 1) == -6000;
    const auto& first = samples[kResumeChunk];
    const bool ramps_in = std::abs(first[0]) < 100 && first[2 * (kChunk - 1)] == 12000;
    std::fprintf(stderr, "servo=%d stretch=%d: starts=%d short-gap=%d ramp-out=%d silent=%d menu-only=%d resumed=%d ramp-in=%d\n",
                 servo, stretch, starts, bridges_gap, ramps_out, silent, menu_only, resumed, ramps_in);
    return starts && bridges_gap && ramps_out && silent && menu_only && resumed && ramps_in;
}

int main() {
    const bool direct = run(false, false);
    const bool rate = run(true, false);
    const bool stretch = run(true, true);
    return direct && rate && stretch ? 0 : 1;
}
