#include "cJSON.h"
#include "device.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static nvs_handle_t store;
esp_err_t storage_init(glance_settings_t *settings, const glance_registry_t *registry) {
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK)
        return err;
    err = nvs_open("glance_v1", NVS_READWRITE, &store);
    if (err != ESP_OK)
        return err;
    glance_settings_defaults(settings);
    char saved[1024], error[96];
    size_t size = sizeof(saved);
    if (nvs_get_str(store, "settings_json", saved, &size) == ESP_OK) {
        cJSON *json = cJSON_ParseWithOpts(saved, NULL, true);
        if (cJSON_IsObject(json)) {
            const cJSON *config_id = cJSON_GetObjectItemCaseSensitive(json, "config_id");
            if (!cJSON_IsString(config_id) || strcmp(config_id->valuestring, GLANCE_CONFIG_ID)) {
                cJSON_DeleteItemFromObjectCaseSensitive(json, "enabled");
                cJSON_DeleteItemFromObjectCaseSensitive(json, "slide_seconds");
            }
            cJSON_DeleteItemFromObjectCaseSensitive(json, "config_id");
            char *patch = cJSON_PrintUnformatted(json);
            if (patch) {
                glance_settings_t restored;
                if (glance_settings_patch(patch, strlen(patch), registry, settings, &restored,
                                          error, sizeof(error)))
                    *settings = restored;
                cJSON_free(patch);
            }
        }
        cJSON_Delete(json);
    }
    return ESP_OK;
}
esp_err_t storage_save_settings(const glance_settings_t *s, const glance_registry_t *registry) {
    cJSON *json = cJSON_CreateObject();
    if (!json)
        return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(json, "config_id", GLANCE_CONFIG_ID);
    cJSON_AddNumberToObject(json, "brightness", s->brightness);
    cJSON_AddNumberToObject(json, "slide_seconds", s->slide_seconds);
    cJSON_AddBoolToObject(json, "night_dim", s->night_dim);
    cJSON *enabled = cJSON_AddArrayToObject(json, "enabled");
    for (size_t i = 0; i < registry->count; i++)
        if (s->enabled[i])
            cJSON_AddItemToArray(enabled, cJSON_CreateString(registry->features[i]->id));
    char *text = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!text)
        return ESP_ERR_NO_MEM;
    esp_err_t err = nvs_set_str(store, "settings_json", text);
    free(text);
    return err == ESP_OK ? nvs_commit(store) : err;
}
bool storage_get_wifi(char ssid[33], char password[65]) {
    size_t n = 33, m = 65;
    ssid[0] = password[0] = 0;
    return nvs_get_str(store, "ssid", ssid, &n) == ESP_OK &&
           nvs_get_str(store, "wifi_pass", password, &m) == ESP_OK && ssid[0];
}
esp_err_t storage_save_wifi(const char *ssid, const char *password) {
    esp_err_t err = nvs_set_str(store, "ssid", ssid);
    if (err != ESP_OK)
        return err;
    err = nvs_set_str(store, "wifi_pass", password);
    return err == ESP_OK ? nvs_commit(store) : err;
}
void storage_setup_password(char out[17]) {
    size_t len = 17;
    if (nvs_get_str(store, "setup_pass", out, &len) == ESP_OK && strlen(out) == 16)
        return;
    uint8_t bytes[8];
    esp_fill_random(bytes, sizeof(bytes));
    for (size_t i = 0; i < 8; i++)
        snprintf(out + i * 2, 3, "%02X", bytes[i]);
    ESP_ERROR_CHECK(nvs_set_str(store, "setup_pass", out));
    ESP_ERROR_CHECK(nvs_commit(store));
}
