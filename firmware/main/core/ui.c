#include "ui.h"
#include <ctype.h>
#include <string.h>

static const struct {
    char c;
    uint8_t rows[7];
} font[] = {
    {'A', {14, 17, 17, 31, 17, 17, 17}}, {'B', {30, 17, 17, 30, 17, 17, 30}},
    {'C', {15, 16, 16, 16, 16, 16, 15}}, {'D', {30, 17, 17, 17, 17, 17, 30}},
    {'E', {31, 16, 16, 30, 16, 16, 31}}, {'F', {31, 16, 16, 30, 16, 16, 16}},
    {'G', {15, 16, 16, 23, 17, 17, 14}}, {'H', {17, 17, 17, 31, 17, 17, 17}},
    {'I', {31, 4, 4, 4, 4, 4, 31}},      {'J', {7, 2, 2, 2, 18, 18, 12}},
    {'K', {17, 18, 20, 24, 20, 18, 17}}, {'L', {16, 16, 16, 16, 16, 16, 31}},
    {'M', {17, 27, 21, 21, 17, 17, 17}}, {'N', {17, 25, 25, 21, 19, 19, 17}},
    {'O', {14, 17, 17, 17, 17, 17, 14}}, {'P', {30, 17, 17, 30, 16, 16, 16}},
    {'Q', {14, 17, 17, 17, 21, 18, 13}}, {'R', {30, 17, 17, 30, 20, 18, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}},   {'T', {31, 4, 4, 4, 4, 4, 4}},
    {'U', {17, 17, 17, 17, 17, 17, 14}}, {'V', {17, 17, 17, 17, 17, 10, 4}},
    {'W', {17, 17, 17, 21, 21, 21, 10}}, {'X', {17, 17, 10, 4, 10, 17, 17}},
    {'Y', {17, 17, 10, 4, 4, 4, 4}},     {'Z', {31, 1, 2, 4, 8, 16, 31}},
    {'0', {14, 17, 19, 21, 25, 17, 14}}, {'1', {4, 12, 4, 4, 4, 4, 14}},
    {'2', {14, 17, 1, 2, 4, 8, 31}},     {'3', {30, 1, 1, 14, 1, 1, 30}},
    {'4', {2, 6, 10, 18, 31, 2, 2}},     {'5', {31, 16, 16, 30, 1, 1, 30}},
    {'6', {14, 16, 16, 30, 17, 17, 14}}, {'7', {31, 1, 2, 4, 8, 8, 8}},
    {'8', {14, 17, 17, 14, 17, 17, 14}}, {'9', {14, 17, 17, 15, 1, 1, 14}},
    {':', {0, 4, 4, 0, 4, 4, 0}},        {'-', {0, 0, 0, 31, 0, 0, 0}},
    {'/', {1, 1, 2, 4, 8, 16, 16}},      {'.', {0, 0, 0, 0, 0, 4, 4}},
    {'*', {0, 21, 14, 31, 14, 21, 0}},   {'+', {0, 4, 4, 31, 4, 4, 0}},
    {'?', {14, 17, 1, 2, 4, 0, 4}},      {' ', {0, 0, 0, 0, 0, 0, 0}},
};
void ui_clear(glance_canvas_t *c) {
    memset(c, 0, sizeof(*c));
}
void ui_pixel(glance_canvas_t *c, int x, int y, bool on) {
    if (!c || x < 0 || x >= GLANCE_WIDTH || y < 0 || y >= GLANCE_HEIGHT)
        return;
    uint8_t mask = (uint8_t)(1u << (y % 8));
    if (on)
        c->pixels[(y / 8) * 128 + x] |= mask;
    else
        c->pixels[(y / 8) * 128 + x] &= (uint8_t)~mask;
}
void ui_box(glance_canvas_t *c, int x, int y, int w, int h, bool fill) {
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++)
            if (fill || yy == y || yy == y + h - 1 || xx == x || xx == x + w - 1)
                ui_pixel(c, xx, yy, true);
}
void ui_text(glance_canvas_t *c, int x, int y, const char *s, int scale) {
    if (!s || scale < 1 || scale > 4)
        return;
    for (; *s && x < GLANCE_WIDTH; s++, x += 6 * scale) {
        unsigned char ch = (unsigned char)*s;
        if (ch >= 128)
            ch = '?';
        else
            ch = (unsigned char)toupper(ch);
        const uint8_t *rows = font[sizeof(font) / sizeof(font[0]) - 2].rows;
        for (size_t i = 0; i < sizeof(font) / sizeof(font[0]); i++)
            if (font[i].c == ch) {
                rows = font[i].rows;
                break;
            }
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (rows[row] & (16 >> col))
                    ui_box(c, x + col * scale, y + row * scale, scale, scale, true);
    }
}
void ui_text_center(glance_canvas_t *c, int y, const char *s, int scale) {
    int width = (int)strlen(s) * 6 * scale - scale;
    ui_text(c, (GLANCE_WIDTH - width) / 2, y, s, scale);
}
void ui_header(glance_canvas_t *c, const char *title) {
    ui_text(c, UI_MARGIN, UI_HEADER_Y, title, 1);
    ui_box(c, UI_MARGIN, UI_RULE_Y, 120, 1, true);
}
void ui_footer(glance_canvas_t *c, const char *status, size_t page, size_t pages) {
    char clipped[16];
    if (pages > GLANCE_FEATURE_MAX)
        pages = GLANCE_FEATURE_MAX;
    size_t limit = (124 - UI_MARGIN) / 6 - pages;
    size_t len = strlen(status);
    if (len > limit)
        len = limit;
    if (len >= sizeof(clipped))
        len = sizeof(clipped) - 1;
    memcpy(clipped, status, len);
    clipped[len] = 0;
    ui_text(c, UI_MARGIN, UI_FOOTER_Y, clipped, 1);
    for (size_t i = 0; i < pages; i++)
        ui_box(c, 124 - (int)(pages - i) * 6, 58, 3, 3, i == page);
}
