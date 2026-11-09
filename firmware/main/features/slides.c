#include "glance.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

#if GLANCE_ROUTE_COUNT
static void render_bus(glance_canvas_t *c, const glance_render_context_t *ctx, const void *data) {
    const glance_route_t *route = data;
    size_t index = (size_t)(route - glance_routes);
    glance_departure_view_t v;
    glance_departure_view(ctx->state, index, ctx->now, &v);
    ui_header(c, route->direction);
    ui_box(c, 4, 17, 28, 32, false);
    int line_scale = strlen(route->line) == 1 ? 3 : 1;
    int line_width = (int)strlen(route->line) * 6 * line_scale - line_scale;
    ui_text(c, 4 + (28 - line_width) / 2, 17 + (32 - 7 * line_scale) / 2, route->line, line_scale);
    char text[16];
    const char *footer;
    if (v.kind == GLANCE_VIEW_LIVE || v.kind == GLANCE_VIEW_SCHEDULED) {
        if (v.minutes == 0)
            snprintf(text, sizeof(text), "DUE");
        else if (v.minutes > 99)
            snprintf(text, sizeof(text), "99+");
        else
            snprintf(text, sizeof(text), "%d", v.minutes);
        ui_text(c, 39, 18, text, 4);
        if (v.minutes > 0 && v.minutes <= 99)
            ui_text(c, 91, 30, "MIN", 2);
        footer = v.kind == GLANCE_VIEW_LIVE ? "LIVE" : "* SCHEDULED";
    } else {
        ui_text(c, 48, 18, "--", 4);
        const char *labels[] = {"WIFI OFFLINE", "SYNCING CLOCK", "FEED ERROR", "DATA STALE",
                                "NO DEPARTURES"};
        footer = v.cancelled ? "TRIP CANCELLED" : labels[v.kind];
    }
    ui_footer(c, footer, ctx->page, ctx->page_count);
}
#endif
static void
    __attribute__((unused)) render_clock(glance_canvas_t *c, const glance_render_context_t *ctx,
                                         const void *unused) {
    (void)unused;
    if (!ctx->state->clock_ready) {
        ui_header(c, GLANCE_NAME);
        ui_text_center(c, 26, "SYNCING CLOCK", 1);
        ui_footer(c, "CONNECT TO WIFI", ctx->page, ctx->page_count);
        return;
    }
    struct tm local;
    localtime_r(&ctx->now, &local);
    char date[24], clock[8];
    strftime(date, sizeof(date), "%a %d %b", &local);
    strftime(clock, sizeof(clock), "%H:%M", &local);
    ui_header(c, date);
    ui_text_center(c, 22, clock, 3);
    ui_footer(c, "", ctx->page, ctx->page_count);
}
void glance_register_builtin_features(glance_registry_t *r) {
#define FEATURE(id, label, render, data) {id, label, render, data},
    static const glance_feature_t features[] = {GLANCE_CONFIG_FEATURES(FEATURE)};
#undef FEATURE
    for (size_t i = 0; i < sizeof(features) / sizeof(features[0]); i++)
        glance_register(r, &features[i]);
}
void glance_render_setup(glance_canvas_t *c, const glance_state_t *s) {
    ui_header(c, "WIFI NOT CONNECTED");
    ui_text(c, 4, 17, s->setup_ssid, 1);
    ui_text(c, 4, 29, "PASSWORD", 1);
    ui_text(c, 4, 38, s->setup_password, 1);
    ui_text(c, 4, 53, "OPEN 192.168.4.1", 1);
}
