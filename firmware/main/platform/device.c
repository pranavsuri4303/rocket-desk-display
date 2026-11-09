#include "device.h"
void device_snapshot(glance_device_t *d, glance_state_t *s, glance_settings_t *settings) {
    xSemaphoreTake(d->lock, portMAX_DELAY);
    if (s)
        *s = d->state;
    if (settings)
        *settings = d->settings;
    xSemaphoreGive(d->lock);
}
