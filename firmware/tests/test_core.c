#include "cJSON.h"
#include "glance.h"
#include "ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static time_t parse(const char *s) {
    time_t result;
    assert(glance_parse_time(s, &result));
    return result;
}
static void timestamps(void) {
    time_t n;
    assert(parse("2026-11-09T12:00:00") == parse("2026-11-09T11:00:00Z"));
    assert(parse("2026-07-09T12:00:00") == parse("2026-07-09T10:00:00Z"));
    assert(parse("2026-11-09T12:00:00+0100") == parse("2026-11-09T11:00:00Z"));
    assert(parse("2026-07-09T12:00:00+02:00") == parse("2026-07-09T10:00:00Z"));
    assert(parse("2026-03-29T03:30:00") - parse("2026-03-29T01:30:00") == 3600);
    const char *bad[] = {"2026-02-30T12:00:00",
                         "2026-13-01T12:00:00",
                         "2026-01-01T24:00:00",
                         "2026-01-01T10:30:00+02",
                         "2026-01-01T10:30:00+01:90",
                         "2026-01-01T10:30:00Zjunk",
                         "",
                         NULL};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        assert(!glance_parse_time(bad[i], &n));
}
static char *fixture(void) {
    return "{\"43005710\":{\"Passes\":{"
           "\"planned\":{\"LinePublicNumber\":\"9\",\"TripStopStatus\":\"PLANNED\","
           "\"ExpectedDepartureTime\":\"2026-11-09T12:01:00\",\"TargetDepartureTime\":\"2026-11-"
           "09T12:01:00\",\"LastUpdateTimeStamp\":\"2026-11-09T12:00:00+0100\"},"
           "\"later\":{\"LinePublicNumber\":\"9\",\"TripStopStatus\":\"DRIVING\","
           "\"ExpectedDepartureTime\":\"2026-11-09T12:16:00\",\"TargetDepartureTime\":\"2026-11-"
           "09T12:16:00\",\"LastUpdateTimeStamp\":\"2026-11-09T12:00:00+0100\"},"
           "\"first\":{\"LinePublicNumber\":\"9\",\"TripStopStatus\":\"DRIVING\","
           "\"ExpectedDepartureTime\":\"2026-11-09T12:06:00\",\"TargetDepartureTime\":\"2026-11-"
           "09T12:04:00\",\"LastUpdateTimeStamp\":\"2026-11-09T11:59:45+0100\"},"
           "\"old\":{\"LinePublicNumber\":\"9\",\"TripStopStatus\":\"DRIVING\","
           "\"ExpectedDepartureTime\":\"2026-11-09T12:02:00\",\"TargetDepartureTime\":\"2026-11-"
           "09T12:02:00\",\"LastUpdateTimeStamp\":\"2026-11-09T11:00:00+0100\"},"
           "\"other\":{\"LinePublicNumber\":\"8\",\"TripStopStatus\":\"DRIVING\","
           "\"ExpectedDepartureTime\":\"2026-11-09T12:02:00\",\"TargetDepartureTime\":\"2026-11-"
           "09T12:02:00\",\"LastUpdateTimeStamp\":\"2026-11-09T12:00:00+0100\"}"
           "}},\"43110060\":{\"Passes\":{}},\"43110270\":{\"Passes\":{}}}";
}
static void transit(void) {
    time_t now = parse("2026-11-09T12:00:00");
    glance_state_t s = {.online = true, .clock_ready = true};
    char *json = fixture();
    assert(glance_transit_parse(json, strlen(json), now, s.routes));
    assert(s.routes[0].count == 4 && s.routes[1].available);
    glance_departure_view_t v;
    glance_departure_view(&s, 0, now, &v);
    assert(v.kind == GLANCE_VIEW_SCHEDULED && v.minutes == 1);
    glance_departure_view(&s, 0, now + 61, &v);
    assert(v.kind == GLANCE_VIEW_SCHEDULED && v.minutes == 1);
    glance_departure_view(&s, 0, now + 121, &v);
    assert(v.kind == GLANCE_VIEW_LIVE && v.minutes == 4 && v.following_minutes == 14 &&
           v.delay_minutes == 2 && v.age_seconds == 136);
    glance_departure_view(&s, 1, now, &v);
    assert(v.kind == GLANCE_VIEW_WAITING);
    glance_departure_view(&s, 0, now + 151, &v);
    assert(v.kind == GLANCE_VIEW_STALE);
    s.online = false;
    glance_departure_view(&s, 0, now, &v);
    assert(v.kind == GLANCE_VIEW_CONNECTING && v.minutes == -1);
    s.online = true;
    s.clock_ready = false;
    glance_departure_view(&s, 0, now, &v);
    assert(v.kind == GLANCE_VIEW_CLOCK_SYNC);
    s.clock_ready = true;
    s.routes[0].items[2].prediction = GLANCE_CANCELLED;
    glance_departure_view(&s, 0, now, &v);
    assert(v.cancelled && v.kind == GLANCE_VIEW_SCHEDULED && v.minutes == 1);
    s.routes[0].items[3].updated = now - 301;
    glance_departure_view(&s, 0, now, &v);
    assert(v.kind == GLANCE_VIEW_SCHEDULED && v.cancelled);
    s.routes[0].items[3].updated = now + 60;
    glance_departure_view(&s, 0, now, &v);
    assert(v.kind == GLANCE_VIEW_SCHEDULED);
    // The live bus stays due between polls, including when fetched just after its ETA.
    time_t due = now + 6 * 60;
    char recent[2048];
    assert(strlen(json) < sizeof(recent));
    strcpy(recent, json);
    char *update = strstr(recent, "11:59:45");
    assert(update);
    memcpy(update, "12:05:45", 8);
    assert(glance_transit_parse(recent, strlen(recent), due + 5, s.routes));
    glance_departure_view(&s, 0, due + 60, &v);
    assert(v.kind == GLANCE_VIEW_LIVE && v.minutes == 0);
    glance_departure_view(&s, 0, due + 61, &v);
    assert(v.kind == GLANCE_VIEW_SCHEDULED && v.minutes == 9);

    assert(!glance_transit_parse("{bad", 4, now, s.routes));
    assert(!glance_transit_parse("[]", 2, now, s.routes));
    assert(!glance_transit_parse("{}garbage", 9, now, s.routes));
    assert(!glance_transit_parse("{}", 65537, now, s.routes));
}
static void settings_and_registry(void) {
    glance_registry_t r = {0};
    glance_register_builtin_features(&r);
    assert(r.count == 4);
    assert(!glance_register(&r, r.features[0]));
    assert(!glance_register(&r, NULL));
    glance_settings_t original, updated;
    glance_settings_defaults(&original);
    char error[128];
    const char *good = "{\"brightness\":64,\"slide_seconds\":12,\"night_dim\":false,\"enabled\":["
                       "\"clock\",\"bus-9-hengelo\"]}";
    assert(
        glance_settings_patch(good, strlen(good), &r, &original, &updated, error, sizeof(error)));
    assert(updated.brightness == 64 && updated.slide_seconds == 12 && !updated.night_dim &&
           !updated.enabled[0] && updated.enabled[1] && updated.enabled[3]);
    const char *bad[] = {"{\"brightness\":256}",
                         "{\"brightness\":-1}",
                         "{\"brightness\":4.5}",
                         "{\"night_dim\":1}",
                         "{\"slide_seconds\":0}",
                         "{\"enabled\":[]}",
                         "{\"enabled\":[\"missing\"]}",
                         "{\"enabled\":[\"clock\",\"clock\"]}",
                         "{\"brightness\":1,\"brightness\":2}",
                         "{\"extra\":true}",
                         "{}garbage"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        glance_settings_t before = updated;
        assert(!glance_settings_patch(bad[i], strlen(bad[i]), &r, &original, &updated, error,
                                      sizeof(error)));
        assert(!memcmp(&before, &updated, sizeof(updated)));
        assert(error[0]);
    }
}
static void renderer(void) {
    struct {
        unsigned char guard1[32];
        glance_canvas_t c;
        unsigned char guard2[32];
    } guarded;
    memset(&guarded, 0xa5, sizeof(guarded));
    ui_clear(&guarded.c);
    ui_box(&guarded.c, -20, -20, 170, 110, true);
    ui_text(&guarded.c, -5, 60, "OVERFLOW IS CLIPPED", 4);
    for (size_t i = 0; i < 32; i++)
        assert(guarded.guard1[i] == 0xa5 && guarded.guard2[i] == 0xa5);
    glance_registry_t r = {0};
    glance_register_builtin_features(&r);
    glance_settings_t settings;
    glance_settings_defaults(&settings);
    time_t now = parse("2026-11-09T12:00:00");
    glance_state_t state = {.online = true, .clock_ready = true};
    assert(glance_transit_parse(fixture(), strlen(fixture()), now, state.routes));
    glance_render_context_t ctx = {
        .now = now, .state = &state, .settings = &settings, .page = 0, .page_count = 4};
    for (size_t i = 0; i < 4; i++) {
        ui_clear(&guarded.c);
        ctx.page = i;
        r.features[i]->render(&guarded.c, &ctx, r.features[i]->data);
    }
    state.routes[0].fetched_at = now - 151;
    ui_clear(&guarded.c);
    ctx.page = 0;
    r.features[0]->render(&guarded.c, &ctx, r.features[0]->data);
    strcpy(state.setup_ssid, "Glance-AB12");
    strcpy(state.setup_password, "DEMO1234DEMO1234");
    ui_clear(&guarded.c);
    glance_render_setup(&guarded.c, &state);
    for (size_t i = 0; i < 32; i++)
        assert(guarded.guard1[i] == 0xa5 && guarded.guard2[i] == 0xa5);
}
int main(void) {
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    timestamps();
    transit();
    settings_and_registry();
    renderer();
    puts("PASS: timestamps/DST, live and scheduled selection, stale/offline/cancelled states, "
         "settings "
         "atomicity, registry and clipped rendering");
}
