#ifndef FOX_RETRO_FONT_H
#define FOX_RETRO_FONT_H

#include <stdint.h>

#define RETRO_FONT_W 8
#define RETRO_FONT_H 16

/* Draw single glyph at (x, y) into a 32-bit ARGB framebuffer of given width/height */
void retro_draw_char(uint32_t *fb, int fb_w, int fb_h, int x, int y, char c, uint32_t fg, uint32_t bg);

/* Draw text line (bg == 0 means transparent background) */
void retro_draw_text(uint32_t *fb, int fb_w, int fb_h, int x, int y, const char *str, uint32_t fg, uint32_t bg);

/* Draw text with a drop shadow (offset +1, +1) */
void retro_draw_text_shadow(uint32_t *fb, int fb_w, int fb_h, int x, int y, const char *str, uint32_t fg, uint32_t shadow);

/* Draw text scaled 2x for big titles */
void retro_draw_text_2x(uint32_t *fb, int fb_w, int fb_h, int x, int y, const char *str, uint32_t fg, uint32_t shadow);

/* Draw filled rectangle */
void retro_fill_rect(uint32_t *fb, int fb_w, int fb_h, int x, int y, int w, int h, uint32_t color);

/* Draw classic 1992 beveled DOS panel */
void retro_draw_panel(uint32_t *fb, int fb_w, int fb_h, int x, int y, int w, int h, uint32_t border_col, uint32_t bg_col);

/* Draw interactive button with hover/active state */
void retro_draw_button(uint32_t *fb, int fb_w, int fb_h, int x, int y, int w, int h, const char *label, int hovered, int selected);

#endif /* FOX_RETRO_FONT_H */
