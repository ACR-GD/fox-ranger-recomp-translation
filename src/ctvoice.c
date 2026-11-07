#include "ctvoice.h"
#include "audio.h"
#include "mem.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>

static struct {
    uint16_t base, irq;
    FarPtr   status;          /* status word address (fn 5) */
    int      have_status, speaker, playing;
} g_ct;

static void set_status(uint16_t v)
{
    if (g_ct.have_status) wr16(g_ct.status.seg, g_ct.status.off, v);
}

void ctvoice_pump(void)
{
    if (g_ct.playing && !audio_voice_busy()) { g_ct.playing = 0; set_status(0); }
}

uint32_t voc_decode(uint16_t seg, uint16_t off, uint8_t **pcm, uint32_t *rate)
{
    uint32_t a = LIN(seg, off), n = 0, cap = 0, r = 0;
    uint8_t *out = NULL;
    uint32_t rep_start = 0, rep_left = 0;
    for (int guard = 0; guard < 4096; guard++) {
        uint8_t type = mem_rd8(a);
        if (type == 0) break;
        uint32_t len = mem_rd8(a + 1) | (mem_rd8(a + 2) << 8) | ((uint32_t)mem_rd8(a + 3) << 16);
        uint32_t body = a + 4, data = 0, dlen = 0, silence = 0;
        switch (type) {
        case 1:                                   /* sound data: rate byte, codec, samples */
            if (!r) r = 1000000u / (256u - mem_rd8(body));
            if (mem_rd8(body + 1) == 0) { data = body + 2; dlen = len >= 2 ? len - 2 : 0; }
            break;
        case 2: data = body; dlen = len; break;   /* continuation */
        case 3:                                   /* silence: u16 length-1, rate byte */
            silence = (uint32_t)(mem_rd8(body) | (mem_rd8(body + 1) << 8)) + 1;
            if (!r) r = 1000000u / (256u - mem_rd8(body + 2));
            break;
        case 6: rep_start = a + 4 + len; rep_left = (uint32_t)(mem_rd8(body) | (mem_rd8(body + 1) << 8)); break;
        case 7:
            if (rep_left && rep_left != 0xFFFF) { rep_left--; a = rep_start; continue; }
            break;                                /* infinite loops (FFFFh) play once */
        default: break;                           /* 4 marker, 5 text, 8/9 extended: skipped */
        }
        uint32_t add = dlen + silence;
        if (n + add > cap) {
            cap = (n + add) * 2 + 1024;
            uint8_t *nb = realloc(out, cap);
            if (!nb) { free(out); *pcm = NULL; return 0; }
            out = nb;
        }
        for (uint32_t k = 0; k < dlen; k++) out[n++] = mem_rd8(data + k);
        for (uint32_t k = 0; k < silence; k++) out[n++] = 0x80;
        a = body + len;
    }
    *pcm = out; *rate = r ? r : 8000;
    return n;
}

void ctvoice_entry(Cpu *c)
{
    ctvoice_pump();
    switch (c->bx) {
    case 0: c->ax = 0x0202; break;                          /* driver version 2.02 */
    case 1: g_ct.base = c->ax; c->ax = 0; break;
    case 2: g_ct.irq = c->ax; c->ax = 0; break;
    case 3: c->ax = 0; break;                               /* init ok */
    case 4: g_ct.speaker = c->al != 0; break;
    case 5: g_ct.status.seg = c->es; g_ct.status.off = c->di; g_ct.have_status = 1; set_status(0); break;
    case 6: {
        uint8_t *pcm; uint32_t rate;
        uint32_t n = voc_decode(c->es, c->di, &pcm, &rate);
        if (n) {
            audio_voice_play(pcm, n, rate);
            g_ct.playing = 1;
            set_status(0xFFFF);
        }
        free(pcm);
        c->ax = 0;
        break; }
    case 7: c->ax = 1; break;                               /* recording unsupported */
    case 8: case 12: audio_voice_stop(); g_ct.playing = 0; set_status(0); break;
    case 9: audio_voice_stop(); g_ct.playing = 0; set_status(0); break;
    case 10: case 11: c->ax = 0; break;                     /* pause/continue: accepted */
    default: fprintf(stderr, "ctvoice: unknown function %u\n", c->bx); c->ax = 1; break;
    }
    c->cf = 0;
    c->sp = (uint16_t)(c->sp + 4);                          /* retf */
}

/* ---- 159E hardware probes: report SB at 220h, IRQ 7, plus AdLib ---- */
#define DSEG SEG(0x1ADF)

/* 159E:01F4 detect -> bitmask 1 = CMS, 2 = AdLib, 4 = SB DSP */
static void sb_detect(Cpu *c) { c->ax = 4 | 2; c->sp = (uint16_t)(c->sp + 4); }

/* 159E:00E8 IRQ probe -> [DS:10F2] = irq, [DS:10EC] = irq, AX = irq */
static void sb_irq_probe(Cpu *c)
{
    wr16(DSEG, 0x10F2, 7); wr8(DSEG, 0x10EC, 7);
    c->ax = 7; c->sp = (uint16_t)(c->sp + 4);
}

/* 159E:036C DSP reset -> AX = 0, CF = 0 */
static void sb_dsp_reset(Cpu *c) { c->ax = 0; c->cf = 0; c->sp = (uint16_t)(c->sp + 4); }

void ctvoice_register(void)
{
    far_register(CTVOICE_SEG, 0x0000, ctvoice_entry, "ctvoice_entry");
    far_register(SBGLUE_SEG, 0x01F4, sb_detect, "sb_detect");
    far_register(SBGLUE_SEG, 0x00E8, sb_irq_probe, "sb_irq_probe");
    far_register(SBGLUE_SEG, 0x036C, sb_dsp_reset, "sb_dsp_reset");
}
