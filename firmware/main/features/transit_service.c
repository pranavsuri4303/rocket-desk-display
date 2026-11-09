#include "device.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <string.h>
#define MAX_BODY 65536
static bool fetch_transit(glance_device_t *d) {
    esp_http_client_config_t config = {.url = GLANCE_TRANSIT_URL,
                                       .timeout_ms = 10000,
                                       .buffer_size = 2048,
                                       .user_agent = "RocketGlance/0.1 personal departure display",
                                       .disable_auto_redirect = true};
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
        return false;
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    char *body = malloc(MAX_BODY + 1);
    size_t count = 0;
    bool ok = false;
    int status = 0;
    if (body && esp_http_client_open(client, 0) == ESP_OK) {
        int64_t length = esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        if (status == 200 && length >= 0 && length <= MAX_BODY) {
            int read = 0;
            while (count < MAX_BODY &&
                   (read = esp_http_client_read(client, body + count, (int)(MAX_BODY - count))) > 0)
                count += (size_t)read;
            if (read >= 0 && esp_http_client_is_complete_data_received(client)) {
                body[count] = 0;
                glance_route_data_t routes[GLANCE_ROUTE_CAPACITY];
                time_t now = time(NULL);
                if (glance_transit_parse(body, count, now, routes)) {
                    xSemaphoreTake(d->lock, portMAX_DELAY);
                    memcpy(d->state.routes, routes, sizeof(routes));
                    xSemaphoreGive(d->lock);
                    ok = true;
                }
            }
        }
    }
    free(body);
    esp_http_client_cleanup(client);
    xSemaphoreTake(d->lock, portMAX_DELAY);
    d->state.last_http_status = status;
    if (ok)
        d->state.fetch_failures = 0;
    else
        d->state.fetch_failures++;
    xSemaphoreGive(d->lock);
    return ok;
}

static void transit_task(void *arg) {
    glance_device_t *d = arg;
    int64_t last_fetch = -60000000;
    unsigned backoff = GLANCE_FETCH_INTERVAL_S;
    for (;;) {
        glance_state_t state;
        device_snapshot(d, &state, NULL);
        int64_t now = esp_timer_get_time();
        if (state.online && state.clock_ready && now - last_fetch >= (int64_t)backoff * 1000000) {
            last_fetch = now;
            bool ok = fetch_transit(d);
            backoff = ok ? GLANCE_FETCH_INTERVAL_S : (backoff < 300 ? backoff * 2 : 600);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
void transit_service_start(glance_device_t *device) {
    if (!glance_route_count)
        return;
    configASSERT(xTaskCreate(transit_task, "glance_transit", 8192, device, 3, NULL) == pdPASS);
}
