#include "glance.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const size_t glance_route_count = GLANCE_ROUTE_COUNT;
#define ROUTE(id, stop_code, line, stop, direction) {id, stop_code, line, stop, direction},
const glance_route_t glance_routes[GLANCE_ROUTE_CAPACITY] = {
#if GLANCE_ROUTE_COUNT
    GLANCE_CONFIG_ROUTES(ROUTE)
#else
    {0}
#endif
};
#undef ROUTE
void glance_settings_defaults(glance_settings_t *s) {
    *s = (glance_settings_t){.brightness = 96,
                             .slide_seconds = GLANCE_SLIDE_SECONDS,
                             .night_dim = true,
                             .enabled = {true, true, true, true, true, true, true, true}};
}
bool glance_register(glance_registry_t *r, const glance_feature_t *f) {
    if (!r || !f || !f->id || !f->label || !f->render || r->count >= GLANCE_FEATURE_MAX)
        return false;
    for (size_t i = 0; i < r->count; i++)
        if (!strcmp(r->features[i]->id, f->id))
            return false;
    r->features[r->count++] = f;
    return true;
}
static int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yo = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    return era * 146097 + (int64_t)(yo * 365 + yo / 4 - yo / 100 + doy) - 719468;
}
bool glance_parse_time(const char *s, time_t *out) {
    if (!s || !out || strlen(s) < 19)
        return false;
    for (int i = 0; i < 19; i++) {
        if (i == 4 || i == 7) {
            if (s[i] != '-')
                return false;
        } else if (i == 10) {
            if (s[i] != 'T')
                return false;
        } else if (i == 13 || i == 16) {
            if (s[i] != ':')
                return false;
        } else if (s[i] < '0' || s[i] > '9')
            return false;
    }
    int y, m, d, h, n, sec;
    if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &m, &d, &h, &n, &sec) != 6)
        return false;
    static const int md[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (y < 2020 || y > 2099 || m < 1 || m > 12 || d < 1 ||
        d > md[m - 1] + (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) || h > 23 ||
        n > 59 || sec > 59)
        return false;
    const char *tail = s + 19;
    if (!*tail) {
        struct tm t = {.tm_year = y - 1900,
                       .tm_mon = m - 1,
                       .tm_mday = d,
                       .tm_hour = h,
                       .tm_min = n,
                       .tm_sec = sec,
                       .tm_isdst = -1};
        *out = mktime(&t);
        return *out != (time_t)-1;
    }
    int offset = 0;
    if (*tail == 'Z' && !tail[1])
        offset = 0;
    else if (*tail == '+' || *tail == '-') {
        int sign = *tail++ == '+' ? 1 : -1, oh = 0, om = 0;
        size_t len = strlen(tail);
        if (len != 4 && len != 5)
            return false;
        if (tail[0] < '0' || tail[0] > '9' || tail[1] < '0' || tail[1] > '9')
            return false;
        const char *mins = tail + 2;
        if (len == 5) {
            if (*mins++ != ':')
                return false;
        }
        if (mins[0] < '0' || mins[0] > '9' || mins[1] < '0' || mins[1] > '9')
            return false;
        oh = (tail[0] - '0') * 10 + tail[1] - '0';
        om = (mins[0] - '0') * 10 + mins[1] - '0';
        if (oh > 14 || om > 59 || (oh == 14 && om))
            return false;
        offset = sign * (oh * 3600 + om * 60);
    } else
        return false;
    *out = (time_t)(days_from_civil(y, (unsigned)m, (unsigned)d) * 86400 + h * 3600 + n * 60 + sec -
                    offset);
    return true;
}
bool glance_departure_at(const glance_departure_t *departure, time_t now,
                         glance_departure_t *effective) {
    *effective = *departure;
    bool fresh = departure->updated && departure->updated <= now + 30 &&
                 now - departure->updated <= GLANCE_PREDICTION_STALE_S;
    if (departure->prediction == GLANCE_CANCELLED)
        return fresh && departure->expected >= now;
    // Keep a bus due for one poll interval unless the feed reports it has passed.
    if (departure->prediction == GLANCE_LIVE && fresh && departure->expected)
        return now <= departure->expected + GLANCE_FETCH_INTERVAL_S;
    if (!departure->scheduled || departure->scheduled < now)
        return false;
    effective->prediction = GLANCE_PLANNED;
    effective->expected = departure->scheduled;
    return true;
}

void glance_departure_view(const glance_state_t *s, size_t route, time_t now,
                           glance_departure_view_t *v) {
    *v = (glance_departure_view_t){
        .kind = GLANCE_VIEW_UNAVAILABLE, .minutes = -1, .following_minutes = -1};
    if (!s || route >= glance_route_count)
        return;
    if (!s->online) {
        v->kind = GLANCE_VIEW_CONNECTING;
        return;
    }
    if (!s->clock_ready) {
        v->kind = GLANCE_VIEW_CLOCK_SYNC;
        return;
    }
    const glance_route_data_t *r = &s->routes[route];
    if (!r->available)
        return;
    if (now < r->fetched_at - 30 || now - r->fetched_at > GLANCE_FETCH_STALE_S) {
        v->kind = GLANCE_VIEW_STALE;
        return;
    }
    v->kind = GLANCE_VIEW_WAITING;
    // A prediction that ages out can fall back to a different timetable time.
    // Compare effective times instead of relying on the order at fetch time.
    glance_departure_t next[2] = {0}, following[2] = {0};
    for (size_t i = 0; i < r->count; i++) {
        glance_departure_t p;
        if (!glance_departure_at(&r->items[i], now, &p))
            continue;
        if (p.prediction == GLANCE_CANCELLED) {
            v->cancelled = true;
            continue;
        }
        size_t index = p.prediction == GLANCE_LIVE ? 1 : 0;
        if (!next[index].expected || p.expected < next[index].expected) {
            following[index] = next[index];
            next[index] = p;
        } else if (!following[index].expected || p.expected < following[index].expected) {
            following[index] = p;
        }
    }
    size_t index = next[1].expected && (!next[0].expected || next[1].expected <= next[0].expected);
    const glance_departure_t *p = &next[index];
    if (!p->expected)
        return;
    v->kind = index ? GLANCE_VIEW_LIVE : GLANCE_VIEW_SCHEDULED;
    v->minutes = p->expected <= now ? 0 : (int)((p->expected - now + 59) / 60);
    if (index) {
        v->age_seconds = (unsigned)(now > p->updated ? now - p->updated : 0);
        if (p->scheduled)
            v->delay_minutes = (int)((p->expected - p->scheduled) / 60);
    }
    time_t second = following[index].expected;
    if (second)
        v->following_minutes = second <= now ? 0 : (int)((second - now + 59) / 60);
}
