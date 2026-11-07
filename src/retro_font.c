#include "retro_font.h"
#include "retro_font_data.h"
#include <string.h>

void retro_draw_char(uint32_t *fb, int fb_w, int fb_h, int x, int y, char c, uint32_t fg, uint32_t bg)
{
    if (x < 0 || x + RETRO_FONT_W > fb_w || y < 0 || y + RETRO_FONT_H > fb_h) return;
    int idx = (unsigned char)c - 32;
    if (idx < 0 || idx >= 95) idx = 0;   /* fallback to space */
    const uint8_t *glyph = g_retro_font_ascii[idx];

    for (int row = 0; row < RETRO_FONT_H; row++) {
        uint8_t bits = glyph[row];
        uint32_t *dst = fb + (y + row) * fb_w + x;
        for (int col = 0; col < RETRO_FONT_W; col++) {
            if (bits & (0x80 >> col)) {
                dst[col] = fg;
            } else if (bg != 0) {
                dst[col] = bg;
            }
        }
    }
}

void retro_draw_text(uint32_t *fb, int fb_w, int fb_h, int x, int y, const char *str, uint32_t fg, uint32_t bg)
{
    if (!str) return;
    int cx = x;
    while (*str) {
        if (*str == '\n') {
            cx = x;
            y += RETRO_FONT_H + 2;
        } else {
            retro_draw_char(fb, fb_w, fb_h, cx, y, *str, fg, bg);
            cx += RETRO_FONT_W;
        }
        str++;
    }
}

void retro_draw_text_shadow(uint32_t *fb, int fb_w, int fb_h, int x, int y, const char *str, uint32_t fg, uint32_t shadow)
{
    retro_draw_text(fb, fb_w, fb_h, x + 1, y + 1, str, shadow, 0);
    retro_draw_text(fb, fb_w, fb_h, x, y, str, fg, 0);
}

void retro_draw_text_2x(uint32_t *fb, int fb_w, int fb_h, int x, int y, const char *str, uint32_t fg, uint32_t shadow)
{
    if (!str) return;
    int cx = x;
    while (*str) {
        int idx = (unsigned char)*str - 32;
        if (idx < 0 || idx >= 95) idx = 0;
        const uint8_t *glyph = g_retro_font_ascii[idx];

        for (int row = 0; row < RETRO_FONT_H; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < RETRO_FONT_W; col++) {
                if (bits & (0x80 >> col)) {
                    int px = cx + col * 2, py = y + row * 2;
                    /* shadow */
                    if (shadow && px + 2 < fb_w && py + 2 < fb_h) {
                        fb[(py + 2) * fb_w + px + 2] = shadow;
                        fb[(py + 2) * fb_w + px + 3] = shadow;
                        fb[(py + 3) * fb_w + px + 2] = shadow;
                        fb[(py + 3) * fb_w + px + 3] = shadow;
                    }
                    /* foreground */
                    if (px + 1 < fb_w && py + 1 < fb_h) {
                        fb[py * fb_w + px] = fg;
                        fb[py * fb_w + px + 1] = fg;
                        fb[(py + 1) * fb_w + px] = fg;
                        fb[(py + 1) * fb_w + px + 1] = fg;
                    }
                }
            }
        }
        cx += RETRO_FONT_W * 2;
        str++;
    }
}

void retro_fill_rect(uint32_t *fb, int fb_w, int fb_h, int x, int y, int w, int h, uint32_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > fb_w) w = fb_w - x;
    if (y + h > fb_h) h = fb_h - y;
    if (w <= 0 || h <= 0) return;

    for (int r = 0; r < h; r++) {
        uint32_t *dst = fb + (y + r) * fb_w + x;
        for (int c = 0; c < w; c++) {
            dst[c] = color;
        }
    }
}

void retro_draw_panel(uint32_t *fb, int fb_w, int fb_h, int x, int y, int w, int h, uint32_t border_col, uint32_t bg_col)
{
    retro_fill_rect(fb, fb_w, fb_h, x, y, w, h, bg_col);

    /* outer border */
    for (int i = 0; i < w; i++) {
        if (x + i >= 0 && x + i < fb_w) {
            if (y >= 0 && y < fb_h) fb[y * fb_w + (x + i)] = border_col;
            if (y + h - 1 >= 0 && y + h - 1 < fb_h) fb[(y + h - 1) * fb_w + (x + i)] = border_col;
        }
    }
    for (int j = 0; j < h; j++) {
        if (y + j >= 0 && y + j < fb_h) {
            if (x >= 0 && x < fb_w) fb[(y + j) * fb_w + x] = border_col;
            if (x + w - 1 >= 0 && x + w - 1 < fb_w) fb[(y + j) * fb_w + (x + w - 1)] = border_col;
        }
    }
}

void retro_draw_button(uint32_t *fb, int fb_w, int fb_h, int x, int y, int w, int h, const char *label, int hovered, int selected)
{
    uint32_t bg = selected ? 0xFF003366 : (hovered ? 0xFF002244 : 0xFF001122);
    uint32_t border = selected ? 0xFFFFAA00 : (hovered ? 0xFF00FFFF : 0xFF006699);
    uint32_t text_col = selected ? 0xFFFFFF00 : (hovered ? 0xFFFFFFFF : 0xFF88CCEE);

    retro_draw_panel(fb, fb_w, fb_h, x, y, w, h, border, bg);

    /* center text in button */
    int text_len = (int)strlen(label);
    int text_w = text_len * RETRO_FONT_W;
    int tx = x + (w - text_w) / 2;
    int ty = y + (h - RETRO_FONT_H) / 2;
    retro_draw_text_shadow(fb, fb_w, fb_h, tx, ty, label, text_col, 0xFF000000);
}
