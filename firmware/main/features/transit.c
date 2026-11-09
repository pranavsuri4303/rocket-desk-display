#include "cJSON.h"
#include "glance.h"
#include <string.h>

static const char *text(const cJSON *o, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}
static void insert(glance_route_data_t *r, const glance_departure_t *p) {
    size_t at = 0;
    while (at < r->count && r->items[at].expected <= p->expected)
        at++;
    if (at >= GLANCE_DEPARTURE_MAX)
        return;
    if (r->count < GLANCE_DEPARTURE_MAX)
        r->count++;
    for (size_t i = r->count - 1; i > at; i--)
        r->items[i] = r->items[i - 1];
    r->items[at] = *p;
}
bool glance_transit_parse(const char *json, size_t length, time_t fetched,
                          glance_route_data_t out[GLANCE_ROUTE_CAPACITY]) {
    if (!json || !out || !length || length > 65536)
        return false;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(json, length, &end, false);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    while (end < json + length &&
           (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t' || !*end))
        end++;
    if (end != json + length) {
        cJSON_Delete(root);
        return false;
    }
    memset(out, 0, sizeof(glance_route_data_t) * GLANCE_ROUTE_CAPACITY);
    bool recognized = false;
    for (size_t i = 0; i < glance_route_count; i++) {
        cJSON *stop = cJSON_GetObjectItemCaseSensitive(root, glance_routes[i].stop_code);
        cJSON *passes = cJSON_GetObjectItemCaseSensitive(stop, "Passes");
        if (!cJSON_IsObject(passes))
            continue;
        recognized = true;
        out[i].available = true;
        out[i].fetched_at = fetched;
        cJSON *p;
        cJSON_ArrayForEach(p, passes) {
            const char *line = text(p, "LinePublicNumber"), *status = text(p, "TripStopStatus"),
                       *kind = text(p, "JourneyStopType");
            if (!line || strcmp(line, glance_routes[i].line) || !status ||
                (kind && !strcmp(kind, "LAST")))
                continue;
            glance_departure_t d = {0};
            if (!strcmp(status, "DRIVING") || !strcmp(status, "ARRIVED"))
                d.prediction = GLANCE_LIVE;
            else if (!strcmp(status, "CANCEL"))
                d.prediction = GLANCE_CANCELLED;
            else if (!strcmp(status, "PLANNED"))
                d.prediction = GLANCE_PLANNED;
            else
                continue;
            glance_parse_time(text(p, "TargetDepartureTime"), &d.scheduled);
            if (d.prediction != GLANCE_PLANNED) {
                glance_parse_time(text(p, "ExpectedDepartureTime"), &d.expected);
                glance_parse_time(text(p, "LastUpdateTimeStamp"), &d.updated);
            }
            if (d.prediction == GLANCE_CANCELLED && !d.expected)
                d.expected = d.scheduled;
            glance_departure_t effective;
            if (glance_departure_at(&d, fetched, &effective))
                insert(&out[i], &effective);
        }
    }
    cJSON_Delete(root);
    return recognized;
}
