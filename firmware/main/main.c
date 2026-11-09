#include "device.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "glance.h"
#include "ui.h"
#include <stdlib.h>
#include <string.h>

void app_main(void) {
    static glance_device_t device;
    device.lock = xSemaphoreCreateMutex();
    configASSERT(device.lock);
    glance_register_builtin_features(&device.registry);
    ESP_ERROR_CHECK(storage_init(&device.settings, &device.registry));
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    ESP_ERROR_CHECK(oled_init());
    network_start(&device);
    api_start(&device);
    transit_service_start(&device);
    static glance_canvas_t canvas, previous;
    bool first = true;
    uint8_t last_brightness = 0;
    size_t page = 0;
    unsigned seen_next = 0;
    int64_t changed = esp_timer_get_time();
    for (;;) {
        glance_state_t state;
        glance_settings_t settings;
        device_snapshot(&device, &state, &settings);
        size_t enabled[GLANCE_FEATURE_MAX], count = 0;
        for (size_t i = 0; i < device.registry.count; i++)
            if (settings.enabled[i])
                enabled[count++] = i;
        if (!count) {
            enabled[0] = 0;
            count = 1;
        }
        if (page >= count)
            page = 0;
        xSemaphoreTake(device.lock, portMAX_DELAY);
        unsigned next = device.next_sequence;
        xSemaphoreGive(device.lock);
        int64_t mono = esp_timer_get_time();
        if (next != seen_next || mono - changed >= (int64_t)settings.slide_seconds * 1000000) {
            page = (page + 1) % count;
            changed = mono;
            seen_next = next;
        }
        time_t now = time(NULL);
        ui_clear(&canvas);
        if (state.setup_active && !state.online)
            glance_render_setup(&canvas, &state);
        else if (!state.online) {
            ui_header(&canvas, GLANCE_NAME);
            ui_text_center(&canvas, 20, "WIFI", 3);
            ui_text_center(&canvas, 51, "RECONNECTING", 1);
        } else {
            glance_render_context_t context = {.now = now,
                                               .state = &state,
                                               .settings = &settings,
                                               .page = page,
                                               .page_count = count};
            const glance_feature_t *feature = device.registry.features[enabled[page]];
            feature->render(&canvas, &context, feature->data);
        }
        uint8_t brightness = settings.brightness;
        if (settings.night_dim && state.clock_ready && !state.setup_active) {
            struct tm local;
            localtime_r(&now, &local);
            if ((local.tm_hour >= 23 || local.tm_hour < 7) && brightness > 8)
                brightness = 8;
        }
        if (first || brightness != last_brightness || memcmp(&canvas, &previous, sizeof(canvas))) {
            esp_err_t result = oled_present(&canvas, brightness);
            if (result == ESP_OK) {
                previous = canvas;
                last_brightness = brightness;
                first = false;
            } else
                ESP_LOGW("oled", "Display transfer failed: %s", esp_err_to_name(result));
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
