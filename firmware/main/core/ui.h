#ifndef GLANCE_UI_H
#define GLANCE_UI_H
#include "glance.h"
// Shared 128 x 64 design tokens. Top-left coordinates; all drawing is clipped.
enum {
    UI_MARGIN = 4,
    UI_HEADER_Y = 2,
    UI_RULE_Y = 12,
    UI_VALUE_Y = 20,
    UI_DETAIL_Y = 45,
    UI_FOOTER_Y = 56
};
void ui_clear(glance_canvas_t *c);
void ui_pixel(glance_canvas_t *c, int x, int y, bool on);
void ui_box(glance_canvas_t *c, int x, int y, int w, int h, bool filled);
void ui_text(glance_canvas_t *c, int x, int y, const char *s, int scale);
void ui_text_center(glance_canvas_t *c, int y, const char *s, int scale);
void ui_header(glance_canvas_t *c, const char *title);
void ui_footer(glance_canvas_t *c, const char *status, size_t page, size_t pages);
#endif
