#ifndef GLANCE_H
#define GLANCE_H
#include "glance_config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define GLANCE_NAME "Rocket Glance"
#define GLANCE_VERSION "0.1.0"
#define GLANCE_API_VERSION 1
#define GLANCE_WIDTH 128
#define GLANCE_HEIGHT 64
#define GLANCE_FEATURE_MAX 8
#define GLANCE_ROUTE_CAPACITY (GLANCE_ROUTE_COUNT ? GLANCE_ROUTE_COUNT : 1)
#define GLANCE_DEPARTURE_MAX 8
#define GLANCE_FETCH_INTERVAL_S 60
#define GLANCE_FETCH_STALE_S 150
#define GLANCE_PREDICTION_STALE_S 300

typedef struct {
    uint8_t pixels[1024];
} glance_canvas_t;
typedef struct {
    uint8_t brightness;
    uint8_t slide_seconds;
    bool night_dim;
    bool enabled[GLANCE_FEATURE_MAX];
} glance_settings_t;
typedef struct {
    const char *id, *stop_code, *line, *stop_label, *direction;
} glance_route_t;
extern const size_t glance_route_count;
extern const glance_route_t glance_routes[GLANCE_ROUTE_CAPACITY];
typedef enum { GLANCE_PLANNED, GLANCE_LIVE, GLANCE_CANCELLED } glance_prediction_t;
typedef struct {
    time_t expected, scheduled, updated;
    glance_prediction_t prediction;
} glance_departure_t;
bool glance_departure_at(const glance_departure_t *departure, time_t now,
                         glance_departure_t *effective);
typedef struct {
    glance_departure_t items[GLANCE_DEPARTURE_MAX];
    size_t count;
    time_t fetched_at;
    bool available;
} glance_route_data_t;
typedef struct {
    glance_route_data_t routes[GLANCE_ROUTE_CAPACITY];
    bool online, clock_ready, setup_active;
    char ip[16], setup_ssid[33], setup_password[17];
    int last_http_status;
    unsigned fetch_failures;
} glance_state_t;
typedef enum {
    GLANCE_VIEW_CONNECTING,
    GLANCE_VIEW_CLOCK_SYNC,
    GLANCE_VIEW_UNAVAILABLE,
    GLANCE_VIEW_STALE,
    GLANCE_VIEW_WAITING,
    GLANCE_VIEW_LIVE,
    GLANCE_VIEW_SCHEDULED
} glance_view_kind_t;
typedef struct {
    glance_view_kind_t kind;
    int minutes, following_minutes, delay_minutes;
    bool cancelled;
    unsigned age_seconds;
} glance_departure_view_t;

typedef struct {
    time_t now;
    const glance_state_t *state;
    const glance_settings_t *settings;
    size_t page, page_count;
} glance_render_context_t;
typedef void (*glance_render_fn)(glance_canvas_t *, const glance_render_context_t *, const void *);
typedef struct {
    const char *id, *label;
    glance_render_fn render;
    const void *data;
} glance_feature_t;
typedef struct {
    const glance_feature_t *features[GLANCE_FEATURE_MAX];
    size_t count;
} glance_registry_t;

void glance_settings_defaults(glance_settings_t *settings);
bool glance_register(glance_registry_t *registry, const glance_feature_t *feature);
void glance_register_builtin_features(glance_registry_t *registry);
bool glance_parse_time(const char *text, time_t *result);
void glance_departure_view(const glance_state_t *state, size_t route, time_t now,
                           glance_departure_view_t *out);
bool glance_transit_parse(const char *json, size_t length, time_t fetched,
                          glance_route_data_t out[GLANCE_ROUTE_CAPACITY]);
bool glance_settings_patch(const char *json, size_t length, const glance_registry_t *registry,
                           const glance_settings_t *current, glance_settings_t *updated,
                           char *error, size_t error_size);
void glance_render_setup(glance_canvas_t *canvas, const glance_state_t *state);
#endif
