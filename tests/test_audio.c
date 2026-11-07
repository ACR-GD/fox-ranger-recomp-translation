/* test_audio.c - headless end-to-end test of the sound path.
 *
 *  1. SFX: transpiled game code (113E voice_init / voice_load / voice_play_bank,
 *     159E wrappers) -> native SB probes + CT-VOICE (15E5:0000) -> mixer.
 *     The rendered PCM must equal the samples of the VOC clip taken straight
 *     from VOICE.DAT (nearest-neighbour 8000 -> 44100 Hz), and the CT-VOICE
 *     status word DS:10EE must go FFFFh -> 0.
 *  2. OPL: AdLib timer detection sequence through ports 388h/389h, then a
 *     test tone; checks level and pitch of Nuked-OPL3 output.
 *  3. DOS path resolution is case-insensitive.
 *
 * usage: test_audio GAME_DIR FOX_UNP2.EXE
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mem.h"
#include "cpu.h"
#include "io.h"
#include "mz_load.h"
#include "heap.h"
#include "dos.h"
#include "crt_shims.h"
#include "ctvoice.h"
#include "audio.h"
#include "recomp_gen.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
                              else { printf("ok   "); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define DS SEG(0x1ADF)

static void call_far(Cpu *c, uint16_t seg, uint16_t off, int nargs, const uint16_t *args)
{
    for (int i = nargs - 1; i >= 0; i--) push16(c, args[i]);
    far_call(c, SEG(seg), off, 0x0050, 0x0000);
    c->sp = (uint16_t)(c->sp + 2 * nargs);                /* cdecl: caller pops */
}

static uint8_t *slurp(const char *p, long *n)
{
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)*n);
    if (fread(d, 1, (size_t)*n, f) != (size_t)*n) { free(d); d = NULL; }
    fclose(f); return d;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s GAME_DIR FOX_UNP2.EXE\n", argv[0]); return 2; }
    const char *game = argv[1];
    MzInfo mz;
    mem_reset();
    if (mz_load(argv[2], 0x1000, " a v", &mz)) return 2;
    dos_init(game);
    heap_init(mz.end_seg, 0xA000);
    crt_register(); crt_init();
    ctvoice_register();
    recomp_register_all();
    audio_init(44100, 0);

    /* ---------------------------------------------------------------- DOS */
    char host[1024];
    CHECK(dos_resolve("VoIcE.DaT", host, sizeof host, 1) == 0 && strstr(host, "VOICE.DAT"), "case-insensitive resolve -> %s", host);
    CHECK(dos_resolve("\\EV22.SYS", host, sizeof host, 1) == 0, "root path \\EV22.SYS -> %s", host);
    CHECK(dos_resolve("C:\\FOX\\NOPE.DAT", host, sizeof host, 1) != 0, "missing file reported");

    /* ---------------------------------------------------------------- SFX */
    Cpu cpu; memset(&cpu, 0, sizeof cpu);
    Cpu *c = &cpu;
    c->ss = 0x8800; c->sp = 0xF000; c->ds = DS; c->es = DS;
    wr8(DS, 0x0B62, 0);                                    /* voice enabled ("fox a v") */
    uint16_t bufseg = heap_alloc_paras(0xF400);
    uint16_t varseg = rd16(DS, 0x1F8E);                    /* buffer far ptr lives at [1F8E]:03BA */
    wr16(varseg, 0x3BA, 0); wr16(varseg, 0x3BC, bufseg);

    call_far(c, 0x113E, 0x0002, 0, NULL);                   /* voice_init */
    CHECK(rd8(DS, 0x0B62) == 0, "voice_init keeps voice enabled (SB detected)");
    CHECK(rd16(DS, 0x10F0) == 0x220 && rd16(DS, 0x10F2) == 7, "SB base %03Xh IRQ %u", rd16(DS, 0x10F0), rd16(DS, 0x10F2));

    uint16_t a8 = 8;
    call_far(c, 0x113E, 0x0178, 1, &a8);                    /* voice_load(8): resident bank */
    long vn; uint8_t *voice = slurp(strcat(strcpy(host, game), "/VOICE.DAT"), &vn);
    CHECK(voice != NULL, "read VOICE.DAT directly");
    int same = 1;
    for (uint32_t k = 0; k < 0xF3E8; k++) if (rd8(bufseg, (uint16_t)k) != voice[0x21566 + k]) { same = 0; break; }
    CHECK(same, "voice_load(8) placed the 62,440-byte SFX bank in the buffer (fopen/fseek/fread shims)");

    uint16_t a10 = 10;
    call_far(c, 0x113E, 0x007B, 1, &a10);                   /* voice_play_bank(10) */
    CHECK(rd16(DS, 0x10EE) == 0xFFFF, "CT-VOICE status word = FFFFh while playing");
    CHECK(audio_voice_busy(), "mixer has a clip");

    /* expected samples: sub-VOC 10 of the bank = file offset 21566 + len[8] + len[9] */
    uint32_t vo = 0x21566 + rd16(DS, 0x0B3E + 16) + rd16(DS, 0x0B3E + 18);
    const uint8_t *voc = voice + vo;
    CHECK(!memcmp(voc, "Creative Voice File", 19), "clip 10 at VOICE.DAT+%05X is a VOC file", vo);
    const uint8_t *blk = voc + (voc[0x14] | voc[0x15] << 8);
    uint32_t blen = blk[1] | blk[2] << 8 | (uint32_t)blk[3] << 16;
    uint32_t rate = 1000000u / (256u - blk[4]), ns = blen - 2;
    const uint8_t *pcm = blk + 6;
    CHECK(blk[0] == 1 && blk[5] == 0 && rate == 8000, "type-1 block, codec 0, %u Hz, %u samples", rate, ns);

    uint32_t frames = (uint32_t)((uint64_t)ns * 44100 / rate) + 4410;
    int16_t *out = malloc(frames * 4);
    audio_render(out, frames);
    uint64_t step = ((uint64_t)rate << 32) / 44100, pos = 0;
    uint32_t mism = 0, played = 0;
    for (uint32_t i = 0; i < frames; i++, pos += step) {
        uint32_t k = (uint32_t)(pos >> 32);
        int32_t want = k < ns ? ((int32_t)pcm[k] - 128) << 7 : 0;
        if (k < ns) played++;
        if (out[2 * i] != want || out[2 * i + 1] != want) mism++;
    }
    CHECK(mism == 0, "rendered %u frames: %u mismatches against VOICE.DAT samples (%u voice frames)", frames, mism, played);
    ctvoice_pump();
    CHECK(rd16(DS, 0x10EE) == 0, "status word back to 0 after the clip");
    free(out);

    /* ---------------------------------------------------------------- OPL */
    port_out8(0x388, 4); port_out8(0x389, 0x60);
    port_out8(0x388, 4); port_out8(0x389, 0x80);
    uint8_t s1 = port_in8(0x388);
    port_out8(0x388, 2); port_out8(0x389, 0xFF);
    port_out8(0x388, 4); port_out8(0x389, 0x21);
    uint8_t s2 = port_in8(0x388);
    CHECK((s1 & 0xE0) == 0 && (s2 & 0xE0) == 0xC0, "AdLib detection: status %02X then %02X", s1, s2);
    port_out8(0x388, 4); port_out8(0x389, 0x60);

    /* modulator silenced (40h = 3Fh): the carrier is a pure sine, so zero crossings measure pitch */
    static const uint8_t tone[][2] = { {0x20, 0x01}, {0x40, 0x3F}, {0x60, 0xF0}, {0x80, 0x77},
        {0x23, 0x01}, {0x43, 0x00}, {0x63, 0xF0}, {0x83, 0x77}, {0xC0, 0x00}, {0xA0, 0x98}, {0xB0, 0x31} };
    for (size_t i = 0; i < sizeof tone / 2; i++) { port_out8(0x388, tone[i][0]); port_out8(0x389, tone[i][1]); }
    uint32_t n = 44100;
    out = malloc(n * 4);
    audio_render(out, n);
    double rms = 0; int cross = 0;
    for (uint32_t i = 2205; i < n; i++) {                     /* skip the attack */
        rms += (double)out[2 * i] * out[2 * i];
        if ((out[2 * i - 2] < 0) != (out[2 * i] < 0)) cross++;
    }
    rms = sqrt(rms / (n - 2205));
    double hz = cross / 2.0 / ((n - 2205) / 44100.0);
    double want_hz = 0x198 * 49716.0 / (1 << (20 - 4));      /* F-num 198h, block 4 */
    CHECK(rms > 500, "OPL tone RMS %.0f", rms);
    CHECK(fabs(hz - want_hz) / want_hz < 0.05, "OPL tone %.1f Hz (expected %.1f Hz)", hz, want_hz);
    AudioStats st = audio_stats();
    CHECK(st.opl_dropped == 0 && st.opl_writes > 0, "%u OPL writes queued, %u dropped", st.opl_writes, st.opl_dropped);
    free(out); free(voice);

    printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
