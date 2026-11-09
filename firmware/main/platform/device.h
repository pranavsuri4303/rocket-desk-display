#ifndef GLANCE_DEVICE_H
#define GLANCE_DEVICE_H
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "glance.h"

typedef struct {
    SemaphoreHandle_t lock;
    glance_state_t state;
    glance_settings_t settings;
    glance_registry_t registry;
    unsigned next_sequence;
} glance_device_t;
void device_snapshot(glance_device_t *d, glance_state_t *state, glance_settings_t *settings);
esp_err_t storage_init(glance_settings_t *settings, const glance_registry_t *registry);
esp_err_t storage_save_settings(const glance_settings_t *settings,
                                const glance_registry_t *registry);
bool storage_get_wifi(char ssid[33], char password[65]);
esp_err_t storage_save_wifi(const char *ssid, const char *password);
void storage_setup_password(char password[17]);
void network_start(glance_device_t *device);
bool network_set_wifi(const char *ssid, const char *password);
void transit_service_start(glance_device_t *device);
void api_start(glance_device_t *device);
esp_err_t oled_init(void);
esp_err_t oled_present(const glance_canvas_t *canvas, uint8_t brightness);
#endif
