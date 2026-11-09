#include "device.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/queue.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "network";
static QueueHandle_t wifi_jobs;
static atomic_bool has_credentials;
typedef struct {
    char ssid[33], password[65];
} wifi_job_t;

static void wifi_events(void *arg, esp_event_base_t base, int32_t event, void *data) {
    glance_device_t *d = arg;
    if (base == WIFI_EVENT && event == WIFI_EVENT_STA_START) {
        if (has_credentials)
            esp_wifi_connect();
    } else if (base == WIFI_EVENT && event == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disconnected = data;
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason %u)", (unsigned)disconnected->reason);
        xSemaphoreTake(d->lock, portMAX_DELAY);
        d->state.online = false;
        d->state.ip[0] = 0;
        xSemaphoreGive(d->lock);
        if (has_credentials)
            esp_wifi_connect();
    } else if (base == IP_EVENT && event == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *got = data;
        esp_err_t sync = esp_netif_sntp_start();
        if (sync != ESP_OK)
            ESP_LOGW(TAG, "Could not start time sync: %s", esp_err_to_name(sync));
        xSemaphoreTake(d->lock, portMAX_DELAY);
        d->state.online = true;
        snprintf(d->state.ip, sizeof(d->state.ip), IPSTR, IP2STR(&got->ip_info.ip));
        xSemaphoreGive(d->lock);
    }
}
static esp_err_t configure_station(const wifi_job_t *job) {
    wifi_config_t c = {0};
    memcpy(c.sta.ssid, job->ssid, strlen(job->ssid));
    memcpy(c.sta.password, job->password, strlen(job->password));
    c.sta.threshold.authmode = job->password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    c.sta.pmf_cfg.capable = true;
    return esp_wifi_set_config(WIFI_IF_STA, &c);
}
static void setup_mode(glance_device_t *d, bool on) {
    ESP_ERROR_CHECK(esp_wifi_set_mode(on ? WIFI_MODE_APSTA : WIFI_MODE_STA));
    xSemaphoreTake(d->lock, portMAX_DELAY);
    d->state.setup_active = on;
    xSemaphoreGive(d->lock);
}
static void network_task(void *arg) {
    glance_device_t *d = arg;
    int64_t last_online = esp_timer_get_time();
    int64_t connected_since = 0;
    for (;;) {
        wifi_job_t job;
        if (xQueueReceive(wifi_jobs, &job, pdMS_TO_TICKS(1000)) == pdTRUE) {
            // Let the phone receive the accepted response before changing networks.
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_err_t saved = storage_save_wifi(job.ssid, job.password);
            if (saved == ESP_OK) {
                has_credentials = false;
                ESP_ERROR_CHECK(esp_wifi_stop());
                ESP_ERROR_CHECK(configure_station(&job));
                has_credentials = true;
                ESP_ERROR_CHECK(esp_wifi_start());
            } else
                ESP_LOGE(TAG, "Could not save Wi-Fi configuration: %s", esp_err_to_name(saved));
            memset(&job, 0, sizeof(job));
            last_online = esp_timer_get_time();
        }
        glance_state_t s;
        device_snapshot(d, &s, NULL);
        int64_t mono = esp_timer_get_time();
        time_t now = time(NULL);
        bool ready = now > 1700000000;
        xSemaphoreTake(d->lock, portMAX_DELAY);
        d->state.clock_ready = ready;
        xSemaphoreGive(d->lock);
        if (s.online) {
            last_online = mono;
            if (!connected_since)
                connected_since = mono;
            if (s.setup_active && mono - connected_since > 10000000)
                setup_mode(d, false);

        } else {
            connected_since = 0;
            if (!s.setup_active && mono - last_online > 60000000)
                setup_mode(d, true);
        }
    }
}
bool network_set_wifi(const char *ssid, const char *password) {
    wifi_job_t job = {0};
    if (!ssid || !password || !ssid[0] || strlen(ssid) > 32 || strlen(password) > 64)
        return false;
    memcpy(job.ssid, ssid, strlen(ssid));
    memcpy(job.password, password, strlen(password));
    bool sent = xQueueSend(wifi_jobs, &job, 0) == pdTRUE;
    memset(&job, 0, sizeof(job));
    return sent;
}
void network_start(glance_device_t *d) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta = esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(esp_netif_set_hostname(sta, "rocket-glance"));
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_events, d));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_events, d));
    wifi_job_t job = {0};
    has_credentials = storage_get_wifi(job.ssid, job.password);
    storage_setup_password(d->state.setup_password);
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));
    snprintf(d->state.setup_ssid, sizeof(d->state.setup_ssid), "Glance-%02X%02X", mac[4], mac[5]);
    // Configure the recovery access point once; it is only enabled while setup is needed.
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    wifi_config_t ap = {0};
    strcpy((char *)ap.ap.ssid, d->state.setup_ssid);
    strcpy((char *)ap.ap.password, d->state.setup_password);
    ap.ap.ssid_len = strlen(d->state.setup_ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    if (has_credentials)
        ESP_ERROR_CHECK(configure_station(&job));
    setup_mode(d, !has_credentials);
    memset(&job, 0, sizeof(job));
    wifi_jobs = xQueueCreate(1, sizeof(wifi_job_t));
    configASSERT(wifi_jobs);
    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("time.cloudflare.com");
    sntp.start = false;
    sntp.wait_for_sync = false;
#if CONFIG_LWIP_SNTP_MAX_SERVERS > 1
    sntp.servers[1] = "pool.ntp.org";
    sntp.num_of_servers = 2;
#endif
    ESP_ERROR_CHECK(esp_netif_sntp_init(&sntp));
    ESP_ERROR_CHECK(esp_wifi_start());
    configASSERT(xTaskCreate(network_task, "glance_network", 8192, d, 4, NULL) == pdPASS);
}
