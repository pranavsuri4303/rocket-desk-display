#include "cJSON.h"
#include "device.h"
#include "esp_http_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char web_start[] asm("_binary_index_html_start");
extern const unsigned char web_end[] asm("_binary_index_html_end");
static esp_err_t send_json(httpd_req_t *req, cJSON *json) {
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!body)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t result = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    free(body);
    return result;
}
static esp_err_t error_response(httpd_req_t *req, const char *status, const char *code,
                                const char *message) {
    httpd_resp_set_status(req, status);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "error", code);
    cJSON_AddStringToObject(j, "message", message);
    return send_json(req, j);
}
static bool read_json(httpd_req_t *req, char *body, size_t capacity) {
    char type[48] = {0};
    httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type));
    if (strcmp(type, "application/json") && strcmp(type, "application/json; charset=utf-8")) {
        error_response(req, "415 Unsupported Media Type", "content_type", "Use application/json");
        return false;
    }
    if (req->content_len <= 0 || (size_t)req->content_len >= capacity) {
        error_response(req, "413 Payload Too Large", "body_size", "JSON body must be 1-1023 bytes");
        return false;
    }
    size_t count = 0;
    while (count < (size_t)req->content_len) {
        int n = httpd_req_recv(req, body + count, req->content_len - count);
        if (n <= 0) {
            error_response(req, "408 Request Timeout", "request_timeout", "Incomplete request");
            return false;
        }
        count += (size_t)n;
    }
    body[count] = 0;
    if (memchr(body, 0, count)) {
        error_response(req, "400 Bad Request", "invalid_json", "NUL bytes are not accepted");
        return false;
    }
    return true;
}
static cJSON *settings_json(const glance_device_t *d, const glance_settings_t *settings) {
    cJSON *j = cJSON_CreateObject(), *enabled = cJSON_AddArrayToObject(j, "enabled");
    cJSON_AddNumberToObject(j, "schema_version", GLANCE_API_VERSION);
    cJSON_AddNumberToObject(j, "brightness", settings->brightness);
    cJSON_AddNumberToObject(j, "slide_seconds", settings->slide_seconds);
    cJSON_AddBoolToObject(j, "night_dim", settings->night_dim);
    for (size_t i = 0; i < d->registry.count; i++)
        if (settings->enabled[i])
            cJSON_AddItemToArray(enabled, cJSON_CreateString(d->registry.features[i]->id));
    return j;
}
static esp_err_t index_get(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)web_start, (ssize_t)(web_end - web_start - 1));
}
static esp_err_t status_get(httpd_req_t *req) {
    glance_device_t *d = req->user_ctx;
    glance_state_t s;
    device_snapshot(d, &s, NULL);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "api_version", 1);
    cJSON_AddStringToObject(j, "name", GLANCE_NAME);
    cJSON_AddStringToObject(j, "firmware", GLANCE_VERSION);
    cJSON_AddBoolToObject(j, "online", s.online);
    cJSON_AddBoolToObject(j, "clock_ready", s.clock_ready);
    cJSON_AddBoolToObject(j, "setup_active", s.setup_active);
    cJSON_AddStringToObject(j, "ip", s.ip);
    cJSON_AddNumberToObject(j, "last_http_status", s.last_http_status);
    cJSON_AddNumberToObject(j, "fetch_failures", s.fetch_failures);
    return send_json(req, j);
}
static esp_err_t features_get(httpd_req_t *req) {
    glance_device_t *d = req->user_ctx;
    cJSON *j = cJSON_CreateObject(), *a = cJSON_AddArrayToObject(j, "features");
    for (size_t i = 0; i < d->registry.count; i++) {
        cJSON *f = cJSON_CreateObject();
        cJSON_AddStringToObject(f, "id", d->registry.features[i]->id);
        cJSON_AddStringToObject(f, "label", d->registry.features[i]->label);
        cJSON_AddItemToArray(a, f);
    }
    return send_json(req, j);
}
static esp_err_t settings_get(httpd_req_t *req) {
    glance_device_t *d = req->user_ctx;
    glance_settings_t s;
    device_snapshot(d, NULL, &s);
    return send_json(req, settings_json(d, &s));
}
static esp_err_t settings_patch(httpd_req_t *req) {
    glance_device_t *d = req->user_ctx;
    char body[1024], error[96];
    if (!read_json(req, body, sizeof(body)))
        return ESP_OK;
    glance_settings_t current, updated;
    device_snapshot(d, NULL, &current);
    if (!glance_settings_patch(body, strlen(body), &d->registry, &current, &updated, error,
                               sizeof(error)))
        return error_response(req, "400 Bad Request", "invalid_settings", error);
    if (storage_save_settings(&updated, &d->registry) != ESP_OK)
        return error_response(req, "500 Internal Server Error", "storage_failed",
                              "Settings were not saved");
    xSemaphoreTake(d->lock, portMAX_DELAY);
    d->settings = updated;
    xSemaphoreGive(d->lock);
    return send_json(req, settings_json(d, &updated));
}
static esp_err_t wifi_put(httpd_req_t *req) {
    char body[1024];
    if (!read_json(req, body, sizeof(body)))
        return ESP_OK;
    const char *end = NULL;
    cJSON *j = cJSON_ParseWithOpts(body, &end, true);
    cJSON *ssid = cJSON_GetObjectItemCaseSensitive(j, "ssid"),
          *pass = cJSON_GetObjectItemCaseSensitive(j, "password");
    bool valid = cJSON_IsObject(j) && cJSON_GetArraySize(j) == 2 && cJSON_IsString(ssid) &&
                 cJSON_IsString(pass);
    if (valid) {
        size_t sl = strlen(ssid->valuestring), pl = strlen(pass->valuestring);
        valid = sl > 0 && sl <= 32 && (pl == 0 || (pl >= 8 && pl <= 63));
    }
    if (!valid) {
        cJSON_Delete(j);
        memset(body, 0, sizeof(body));
        return error_response(
            req, "400 Bad Request", "invalid_wifi",
            "Use an SSID of 1-32 bytes and a password of 8-63 bytes, or empty for an open network");
    }
    bool accepted = network_set_wifi(ssid->valuestring, pass->valuestring);
    cJSON_Delete(j);
    memset(body, 0, sizeof(body));
    if (!accepted)
        return error_response(req, "503 Service Unavailable", "busy",
                              "A Wi-Fi change is already pending");
    httpd_resp_set_status(req, "202 Accepted");
    j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "status", "connecting");
    return send_json(req, j);
}
static esp_err_t next_post(httpd_req_t *req) {
    glance_device_t *d = req->user_ctx;
    char body[1024];
    if (!read_json(req, body, sizeof(body)))
        return ESP_OK;
    cJSON *j = cJSON_ParseWithOpts(body, NULL, true);
    bool valid = cJSON_IsObject(j) && cJSON_GetArraySize(j) == 0;
    cJSON_Delete(j);
    if (!valid)
        return error_response(req, "400 Bad Request", "invalid_action",
                              "Send an empty JSON object");
    xSemaphoreTake(d->lock, portMAX_DELAY);
    d->next_sequence++;
    xSemaphoreGive(d->lock);
    j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "ok", true);
    return send_json(req, j);
}
static esp_err_t departures_get(httpd_req_t *req) {
    glance_device_t *d = req->user_ctx;
    glance_state_t s;
    device_snapshot(d, &s, NULL);
    time_t now = time(NULL);
    cJSON *j = cJSON_CreateObject(), *a = cJSON_AddArrayToObject(j, "departures");
    const char *states[] = {"offline",          "clock_sync", "unavailable", "stale",
                            "waiting_for_live", "live",       "scheduled"};
    for (size_t i = 0; i < glance_route_count; i++) {
        glance_departure_view_t v;
        glance_departure_view(&s, i, now, &v);
        cJSON *r = cJSON_CreateObject();
        cJSON_AddStringToObject(r, "id", glance_routes[i].id);
        cJSON_AddStringToObject(r, "stop", glance_routes[i].stop_label);
        cJSON_AddStringToObject(r, "direction", glance_routes[i].direction);
        cJSON_AddStringToObject(r, "state", states[v.kind]);
        if (v.kind == GLANCE_VIEW_LIVE) {
            cJSON_AddNumberToObject(r, "minutes", v.minutes);
            cJSON_AddNumberToObject(r, "delay_minutes", v.delay_minutes);
            cJSON_AddNumberToObject(r, "prediction_age_seconds", v.age_seconds);
        } else if (v.kind == GLANCE_VIEW_SCHEDULED)
            cJSON_AddNumberToObject(r, "minutes", v.minutes);
        else
            cJSON_AddNullToObject(r, "minutes");
        if (v.following_minutes >= 0)
            cJSON_AddNumberToObject(r, "following_minutes", v.following_minutes);
        else
            cJSON_AddNullToObject(r, "following_minutes");
        cJSON_AddBoolToObject(r, "cancellation", v.cancelled);
        cJSON_AddItemToArray(a, r);
    }
    return send_json(req, j);
}
void api_start(glance_device_t *device) {
    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.max_uri_handlers = 10;
    c.stack_size = 8192;
    c.lru_purge_enable = true;
    c.recv_wait_timeout = 5;
    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &c));
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index_get, .user_ctx = device},
        {.uri = "/api/v1/status", .method = HTTP_GET, .handler = status_get, .user_ctx = device},
        {.uri = "/api/v1/features",
         .method = HTTP_GET,
         .handler = features_get,
         .user_ctx = device},
        {.uri = "/api/v1/settings",
         .method = HTTP_GET,
         .handler = settings_get,
         .user_ctx = device},
        {.uri = "/api/v1/settings",
         .method = HTTP_PATCH,
         .handler = settings_patch,
         .user_ctx = device},
        {.uri = "/api/v1/wifi", .method = HTTP_PUT, .handler = wifi_put, .user_ctx = device},
        {.uri = "/api/v1/actions/next",
         .method = HTTP_POST,
         .handler = next_post,
         .user_ctx = device},
        {.uri = "/api/v1/departures",
         .method = HTTP_GET,
         .handler = departures_get,
         .user_ctx = device},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++)
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &routes[i]));
}
