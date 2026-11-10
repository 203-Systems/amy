// Tests pcm_get_memory_preset(), the read-back an embedder uses to save a
// load_sample / start_sample preset to disk.
//
// It must hand back the preset's real shape -- frame count, channel count,
// sample rate, tuning -- since a mono load_sample preset and a stereo
// start_sample recording are both just int16 RAM, and a caller that guesses
// the channel count reads past the end of a mono buffer.  And it must NOT
// fall back to a ROM preset the way pcm_get_sample_ram_for_preset() does: an
// unknown preset number is "nothing to save", not "ROM preset 0".
//
// Build/run with `make ctest`.

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "amy.h"

static int failures = 0;

#define CHECK(cond, fmt, ...) do {                                        \
    if (cond) { printf("  ok   " fmt "\n", ##__VA_ARGS__); }              \
    else { printf("  FAIL " fmt "\n", ##__VA_ARGS__); failures++; }       \
} while (0)

static void test_mono_load(void) {
    printf("mono pcm_load preset reads back its shape and data\n");
    int16_t *ram = pcm_load(301, 1000, 22050, 1, 60.04f, 0, 1000);
    for (int i = 0; i < 1000; ++i) ram[i] = (int16_t)(i * 7);
    uint32_t frames = 0, sr = 0; uint8_t ch = 0; float note = 0;
    const int16_t *got = pcm_get_memory_preset(301, &frames, &ch, &sr, &note);
    CHECK(got == ram, "returns the preset's sample RAM");
    CHECK(frames == 1000, "frames = 1000 (got %u)", (unsigned)frames);
    CHECK(ch == 1, "channels = 1 (got %u)", ch);
    CHECK(sr == 22050, "samplerate = 22050 (got %u)", (unsigned)sr);
    CHECK(note > 60.03f && note < 60.05f, "midinote = 60.04 (got %f)", note);
    CHECK(got && got[999] == (int16_t)(999 * 7), "data intact");
}

static void test_stereo_record(void) {
    printf("start_sample preset is stereo at AMY's rate\n");
    char start[] = "zS302,1,4800,60,0,0Z";
    amy_add_message(start);
    for (int i = 0; i < 8; ++i) amy_simple_fill_buffer();
    uint32_t frames = 0, sr = 0; uint8_t ch = 0;
    const int16_t *got = pcm_get_memory_preset(302, &frames, &ch, &sr, NULL);
    CHECK(got != NULL, "recording preset found");
    CHECK(frames == 4800, "frames = max_frames 4800 (got %u)", (unsigned)frames);
    CHECK(ch == 2, "channels = 2 (got %u)", ch);
    CHECK(sr == AMY_SAMPLE_RATE, "samplerate = AMY_SAMPLE_RATE (got %u)", (unsigned)sr);
    char stop[] = "zO1Z";
    amy_add_message(stop);
    amy_simple_fill_buffer();
}

static void test_no_rom_fallback(void) {
    printf("non-memory presets return NULL\n");
    uint32_t frames = 12345;
    CHECK(pcm_get_memory_preset(0, &frames, NULL, NULL, NULL) == NULL, "ROM preset 0 -> NULL");
    CHECK(frames == 12345, "out-params untouched on miss");
    CHECK(pcm_get_memory_preset(4000, NULL, NULL, NULL, NULL) == NULL, "unknown preset -> NULL");
    pcm_unload_preset(301);
    CHECK(pcm_get_memory_preset(301, NULL, NULL, NULL, NULL) == NULL, "unloaded preset -> NULL");
}

// Renders `blocks` blocks of osc 0 playing preset # at its own pitch into out (mono, the first channel).
static void render_preset(int preset, int16_t *out, int blocks) {
    char msg[64];
    amy_add_message("v0l0");
    for (int i = 0; i < 2; ++i) amy_simple_fill_buffer();
    snprintf(msg, sizeof msg, "v0w%dp%dl1", PCM, preset);
    amy_add_message(msg);
    for (int b = 0; b < blocks; ++b) {
        int16_t *buf = amy_simple_fill_buffer();
        for (int i = 0; i < AMY_BLOCK_SIZE; ++i) out[b * AMY_BLOCK_SIZE + i] = buf[i * AMY_NCHANS];
    }
}

static int16_t external_frames[3000];

static void test_external(void) {
    printf("pcm_load_external plays the caller's frames in place\n");
    for (int i = 0; i < 3000; ++i) external_frames[i] = (int16_t)((i % 50) * 400 - 10000);
    CHECK(pcm_load_external(303, external_frames, 3000, 22050, 1, 60, 0, 0) == 1, "registered");
    uint32_t frames = 0, sr = 0; uint8_t ch = 0;
    const int16_t *got = pcm_get_memory_preset(303, &frames, &ch, &sr, NULL);
    CHECK(got == external_frames, "the preset is the caller's frames, not a copy");
    CHECK(frames == 3000 && ch == 1 && sr == 22050, "shape kept (%u frames, %u ch, %u Hz)", (unsigned)frames, ch, (unsigned)sr);
    int16_t *copy = pcm_load(304, 3000, 22050, 1, 60, 0, 0);
    memcpy(copy, external_frames, sizeof external_frames);
    enum { kBlocks = 8 };
    static int16_t a[kBlocks * AMY_BLOCK_SIZE], b[kBlocks * AMY_BLOCK_SIZE];
    render_preset(303, a, kBlocks);
    render_preset(304, b, kBlocks);
    int loud = 0;
    for (int i = 0; i < kBlocks * AMY_BLOCK_SIZE; ++i) if (a[i] > 100 || a[i] < -100) loud = 1;
    int peak = 0; for (int i = 0; i < kBlocks * AMY_BLOCK_SIZE; ++i) { int v = a[i] < 0 ? -a[i] : a[i]; if (v > peak) peak = v; }
    CHECK(loud, "it sounds (peak %d)", peak);
    CHECK(memcmp(a, b, sizeof a) == 0, "sounds exactly as the same frames loaded with pcm_load");
    amy_add_message("v0l0");
    amy_simple_fill_buffer();
    pcm_unload_preset(303);
    pcm_unload_preset(304);
    CHECK(pcm_get_memory_preset(303, NULL, NULL, NULL, NULL) == NULL, "unloaded");
    CHECK(external_frames[2999] == (int16_t)((2999 % 50) * 400 - 10000), "the caller's frames are left alone");
    CHECK(pcm_load_external(305, NULL, 3000, 22050, 1, 60, 0, 0) == 0, "no frames: refused");
    CHECK(pcm_get_memory_preset(305, NULL, NULL, NULL, NULL) == NULL, "nothing registered for it");
}

// AMY calls these; the test binary has to provide them.
void delay_ms(uint32_t ms) { (void)ms; }

int main(void) {
    amy_config_t c = amy_default_config();
    c.features.startup_bleep = 0;
    amy_start(c);
    for (int i = 0; i < 4; ++i) amy_simple_fill_buffer();

    test_mono_load();
    test_stereo_record();
    test_no_rom_fallback();
    test_external();

    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("all ok\n");
    return 0;
}
